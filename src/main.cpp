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

using namespace std;

string get_path(string command) {
  string path_env = std::getenv("PATH");
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

// --- AUTOCOMPLETE LOGIC START ---

// 1. The Generator: Returns the next matching builtin command each time it is called
char* command_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index = 0;
  
  // 'state' is 0 the first time this is called for a new tab press
  if (state == 0) {
    matches.clear();
    match_index = 0;
    string query = text;
    
    // List of builtins to autocomplete
    vector<string> builtins = {"echo", "exit", "type", "pwd", "cd"};
    
    for(const auto& cmd : builtins) {
      if(cmd.find(query) == 0) { // Check if 'cmd' starts with 'query'
        matches.push_back(cmd);
      }
    }
  }

  // Return the next match, or NULL if no more matches
  if (match_index >= matches.size()) {
      return nullptr;
  }
  
  // readline requires malloc'd strings, so we use strdup
  return strdup(matches[match_index++].c_str());
}

// 2. The Hook: Decides when to use our custom generator
char** shell_completion(const char* text, int start, int end) {
    // Only attempt to complete the FIRST word (the command)
    // 'start' is the index of the cursor in the line buffer
    if (start == 0) {
        // rl_completion_matches calls our generator until it returns NULL
        return rl_completion_matches(text, command_generator);
    }
    
    // If we are not at the start (completing arguments), return NULL
    // This tells readline to fall back to default filename completion
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