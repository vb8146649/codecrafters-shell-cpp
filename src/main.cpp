#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> 
#include <unistd.h> 
#include <filesystem>
#include <fcntl.h> 
#include <cstring> // Required for strdup
#include <readline/readline.h> // Required for readline
#include <readline/history.h>  // Required for add_history
#include <set>
#include <sys/wait.h>
using namespace std;

string get_path(string command) {
  string path_env = getenv("PATH");
  stringstream ss(path_env);
  string path;

  while (!ss.eof()) {
    getline(ss, path, ':');
    string abs_path = path + "/" + command;
    if (access(abs_path.c_str(), X_OK) == 0) {
      return abs_path;
    }
  }
  return "";
}

// Helper to parse arguments, handling quotes and spaces
vector<string> parse_input(string input) {
    vector<string> args;
    string current_arg;
    bool in_single = false;
    bool in_double = false;
    
    for (int i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '\'') {
            if (in_double) current_arg += c; 
            else in_single = !in_single; 
        } else if (c == '"') {
            if (in_single) current_arg += c; 
            else in_double = !in_double; 
        } else if (c == ' ') {
            if (in_single || in_double) current_arg += c; 
            else if (!current_arg.empty()) {
                args.push_back(current_arg); 
                current_arg = "";
            }
        } else {
            current_arg += c; 
        }
    }
    if (!current_arg.empty()) args.push_back(current_arg);
    return args;
}

// Helper function to trim whitespace
string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}
// --- AUTOCOMPLETE LOGIC START ---

// 1. The Generator: Returns the next matching builtin command each time it is called
char* command_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index = 0;
  
  // 'state' is 0 when the user hits TAB for the first time
  if (state == 0) {
    matches.clear();
    match_index = 0;
    string query = text;
    
    // Use a set to automatically handle duplicates (e.g. if 'git' is in /bin and /usr/bin)
    set<string> match_set;

    // --- Part A: Add Builtins ---
    vector<string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    for(const auto& cmd : builtins) {
      if(cmd.find(query) == 0) { 
        match_set.insert(cmd);
      }
    }

    // --- Part B: Add Executables from PATH ---
    string path_env = std::getenv("PATH");
    stringstream ss(path_env);
    string path;

    while (getline(ss, path, ':')) {
        // Skip directory if it doesn't exist
        if (!filesystem::exists(path)) continue;

        try {
            // Iterate over every file in the directory
            for (const auto& entry : filesystem::directory_iterator(path)) {
                string filename = entry.path().filename().string();
                
                // 1. Check if filename starts with our query
                if (filename.find(query) == 0) {
                    // 2. Check if we have Execute (X_OK) permission
                    if (access(entry.path().c_str(), X_OK) == 0) {
                        match_set.insert(filename);
                    }
                }
            }
        } catch (...) {
            // Gracefully handle permission errors or other FS issues
        }
    }
    
    // Move unique matches from set to the vector for readline to consume
    for (const auto& m : match_set) {
        matches.push_back(m);
    }
  }

  // Return the next match, or NULL if no more matches
  if (match_index >= matches.size()) {
      return nullptr;
  }
  
  return strdup(matches[match_index++].c_str());
}

// 2. The Hook: Decides when to use our custom generator
char** shell_completion(const char* text, int start, int end) {
    // Only attempt to complete the FIRST word (the command)
    if (start == 0) {
        // 1. Get the matches from your generator
        char** matches = rl_completion_matches(text, command_generator);
        
        // 2. Check if matches is NULL (Empty)
        if (matches == nullptr) {
            // 3. Ring the Bell explicitly
            cout << '\a' << flush; 
        }
        
        return matches;
    }
    
    // If we are not at the start, fall back to default filename completion
    return nullptr; 
}

// --- AUTOCOMPLETE LOGIC END ---

int main() {
  // Configure readline to use our completion function
  rl_attempted_completion_function = shell_completion;

  while(true){
    // Use readline for input. It handles the prompt "$ " and the <TAB> key automatically.
    char* input_cstr = readline("$ ");
    
    // Check for EOF (Ctrl+D)
    if (!input_cstr) {
        break; 
    }
    
    string input = input_cstr;
    
    // Add non-empty commands to history (allows Up/Down arrow usage)
    if (!input.empty()) {
        add_history(input_cstr);
    }
    
    // Free the buffer allocated by readline
    free(input_cstr);

    // --- PIPELINE PARSING (Quote-Aware) ---
    int pipe_pos = -1;
    bool in_quotes_pipe = false;
    char quote_char_pipe = 0;

    for (int i = 0; i < input.length(); i++) {
        if (input[i] == '\'' || input[i] == '"') {
            if (!in_quotes_pipe) {
                in_quotes_pipe = true;
                quote_char_pipe = input[i];
            } else if (input[i] == quote_char_pipe) {
                in_quotes_pipe = false;
            }
        }
        if (!in_quotes_pipe && input[i] == '|') {
            pipe_pos = i;
            break;
        }
    }

    if (pipe_pos != -1) {
        string cmd1_str = trim(input.substr(0, pipe_pos));
        string cmd2_str = trim(input.substr(pipe_pos + 1));
        vector<string> args1 = parse_input(cmd1_str);
        vector<string> args2 = parse_input(cmd2_str);
        
        if(!args1.empty() && !args2.empty()) {
            int pipefd[2];
            if (pipe(pipefd) == -1) { perror("pipe"); continue; }

            pid_t pid1 = fork();
            if (pid1 == 0) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]); close(pipefd[1]);
                vector<char*> c_args;
                for(const auto& arg : args1) c_args.push_back(strdup(arg.c_str()));
                c_args.push_back(nullptr);
                execvp(c_args[0], c_args.data());
                exit(1);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[1]); close(pipefd[0]);
                vector<char*> c_args;
                for(const auto& arg : args2) c_args.push_back(strdup(arg.c_str()));
                c_args.push_back(nullptr);
                execvp(c_args[0], c_args.data());
                exit(1);
            }

            close(pipefd[0]); close(pipefd[1]);
            waitpid(pid1, nullptr, 0);
            waitpid(pid2, nullptr, 0);
            continue; 
        }
    }

    // --- REDIRECTION LOGIC START (Unchanged) ---
    int saved_stdout = dup(STDOUT_FILENO); 
    int saved_stderr = dup(STDERR_FILENO);
    bool redirectout = false;
    bool redirecterr = false;
    bool append_mode = false;
    string outfile;
    string clean_input = input; 

    bool in_quotes = false;
    char quote_char = 0;
    int redirect_pos = -1;
    int redirect_len = 0;
    int target_fd = STDOUT_FILENO;

    for (int i = 0; i < input.length(); i++) {
        if (input[i] == '\'' || input[i] == '"') {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = input[i];
            } else if (input[i] == quote_char) {
                in_quotes = false;
            }
        }
        
        if (!in_quotes) {
            if (input[i] == '>') {
                redirect_pos = i;
                redirect_len = 1;
                redirectout = true;
                
                // Check for Append (>>)
                if (i + 1 < input.length() && input[i+1] == '>') {
                    append_mode = true;
                    redirect_len = 2;
                }

                if (i > 0) {
                    if (input[i-1] == '1') {
                        redirect_pos = i - 1;
                        redirect_len += 1;
                        redirectout = true;
                        redirecterr = false;
                    } else if (input[i-1] == '2') {
                        redirect_pos = i - 1;
                        redirect_len += 1;
                        redirecterr = true;
                        redirectout = false;
                    }
                }
                break; 
            }
        }
    }

    if (redirect_pos != -1) {
        clean_input = input.substr(0, redirect_pos);
        string raw_file = input.substr(redirect_pos + redirect_len);
        size_t first = raw_file.find_first_not_of(" \t");
        if(first != string::npos) {
            size_t last = raw_file.find_last_not_of(" \t");
            outfile = raw_file.substr(first, (last - first + 1));
        }
    }

    if((redirectout || redirecterr) && !outfile.empty()) {
        int flags = O_WRONLY | O_CREAT;
        if (append_mode) flags |= O_APPEND;
        else flags |= O_TRUNC;

        int fd = open(outfile.c_str(), flags, 0644);
        if (fd < 0) {
            perror("open");
        } else if (redirectout) {
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else if (redirecterr) {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        input = clean_input;
    }
    // --- REDIRECTION LOGIC END ---
    
    vector<string> args = parse_input(clean_input);
    if (args.empty()) {
         // Restore and continue
         dup2(saved_stdout, STDOUT_FILENO);
         dup2(saved_stderr, STDERR_FILENO);
         close(saved_stdout);
         close(saved_stderr);
         continue; 
    }
    
    string command = args[0];

    if(command == "exit"){
      break;
    } else if(command=="echo"){
      for (size_t i = 1; i < args.size(); ++i) {
          cout << args[i];
          if (i < args.size() - 1) cout << " ";
      }
      cout << endl;
    } else if(command=="type"){
      if (args.size() < 2) continue;
      string arg = args[1]; 
      
      if(arg == "echo" || arg == "exit" || arg == "type" || arg == "pwd" || arg=="cd") {
        cout << arg << " is a shell builtin" << endl;
      } else {
        string path = get_path(arg);
        if (!path.empty()) {
          cout << arg << " is " << path << endl;
        } else {
          cout << arg << ": not found" << endl;
        }
      }
    } else if(command=="pwd"){
      cout<<filesystem::current_path().string()<<endl;
    } else if(command=="cd"){
      string path;
      path=args.size() > 1 ? args[1] : "~";
      if(chdir(path.c_str())==0 && path!="~"){
      } else if(path=="~"){
        const char* home = getenv("HOME");
        if(home) chdir(home);
      } else {
        cout << "cd: " << path << ": No such file or directory" << endl;
      }
    } else {
        string path = get_path(command);
        if (!path.empty()) {
          system(input.c_str());
        } else {
          cout<<input<<": command not found" << endl;
        }
    }

    // --- RESTORE FDS ---
    fflush(stdout); 
    fflush(stderr);
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
  };
}