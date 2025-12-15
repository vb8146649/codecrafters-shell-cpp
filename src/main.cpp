#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> 
#include <unistd.h> 
#include <filesystem>
#include <fcntl.h> 
#include <cstring> 
#include <readline/readline.h> 
#include <readline/history.h>  
#include <set>
#include <algorithm>
#include <sys/wait.h> 
#include <fstream>

using namespace std;

const vector<string> builtins={"exit", "echo", "type", "pwd", "cd", "history"};
// --- Helper Functions ---
vector<string> command_history;

string get_path(string command) {
  string path_env = std::getenv("PATH");
  stringstream ss(path_env);
  string path;
  while (getline(ss, path, ':')) {
    string abs_path = path + "/" + command;
    if (access(abs_path.c_str(), X_OK) == 0) {
      return abs_path;
    }
  }
  return "";
}

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

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

// Split input string by '|' respecting quotes
vector<string> split_pipeline(string input) {
    vector<string> commands;
    bool in_quotes = false;
    char quote_char = 0;
    int start = 0;

    for (int i = 0; i < input.length(); i++) {
        if (input[i] == '\'' || input[i] == '"') {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = input[i];
            } else if (input[i] == quote_char) {
                in_quotes = false;
            }
        }
        
        if (!in_quotes && input[i] == '|') {
            commands.push_back(trim(input.substr(start, i - start)));
            start = i + 1;
        }
    }
    // Add the final command
    commands.push_back(trim(input.substr(start)));
    return commands;
}

// --- Centralized Builtin Logic ---
bool handle_builtin(const vector<string>& args) {
    if (args.empty()) return false;
    string command = args[0];

    if (command == "exit") {
        exit(0);
    } 
    else if (command == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            cout << args[i];
            if (i < args.size() - 1) cout << " ";
        }
        cout << endl;
        return true;
    } 
    else if (command == "type") {
        if (args.size() > 1) {
            string arg = args[1];
            if (find(builtins.begin(), builtins.end(), arg) != builtins.end()) {
                cout << arg << " is a shell builtin" << endl;
            } else {
                string p = get_path(arg);
                if (!p.empty()) cout << arg << " is " << p << endl;
                else cout << arg << ": not found" << endl;
            }
        }
        return true;
    } 
    else if (command == "pwd") {
        cout << filesystem::current_path().string() << endl;
        return true;
    } 
    else if (command == "cd") {
        string p = (args.size() > 1) ? args[1] : "~";
        if (p == "~") {
            const char* home = getenv("HOME");
            if (home) chdir(home);
        } else {
            if (chdir(p.c_str()) != 0) {
                cout << "cd: " << p << ": No such file or directory" << endl;
            }
        }
        return true;
    }
    else if(command == "history") {
        // --- NEW: Handle 'history -r filename' ---
        if(args.size() > 1 && args[1] == "-r") {
            if(args.size() < 3) {
                cout << "history: option -r requires an argument" << endl;
                return true;
            }
            
            string filepath = args[2];
            ifstream history_file(filepath);
            
            if (!history_file.is_open()) {
                cout << "history: " << filepath << ": No such file or directory" << endl;
                return true;
            }

            string line;
            while (getline(history_file, line)) {
                if(!line.empty()) {
                    command_history.push_back(line);
                }
            }
            history_file.close();
            return true;
        }
        // --- End of New Logic ---

        if(args.size() > 1 && args[1] == "-c") {
            command_history.clear();
            return true;
        }
        
        // Handle printing history (default behavior)
        size_t start_index = 0;
        if(args.size() > 1) {
            // If argument is a number (e.g., "history 5"), show last N commands
            try {
                int n = stoi(args[1]);
                if (n < command_history.size()) {
                    start_index = command_history.size() - n;
                }
            } catch (...) {
                // If parsing fails (e.g. invalid flags), ignore or handle error
            }
        }

        for (size_t i = start_index; i < command_history.size(); ++i) {
            cout << "    " << (i + 1) << "  " << command_history[i] << endl;
        }
        return true;
    }
    return false;
}

// --- Autocomplete Logic ---
char* command_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index = 0;
  if (state == 0) {
    matches.clear();
    match_index = 0;
    string query = text;
    set<string> match_set;
    for(const auto& cmd : builtins) if(cmd.find(query) == 0) match_set.insert(cmd);

    string path_env = getenv("PATH");
    stringstream ss(path_env);
    string path;
    while (getline(ss, path, ':')) {
        if (!filesystem::exists(path)) continue;
        try {
            for (const auto& entry : filesystem::directory_iterator(path)) {
                string filename = entry.path().filename().string();
                if (filename.find(query) == 0 && access(entry.path().c_str(), X_OK) == 0) {
                    match_set.insert(filename);
                }
            }
        } catch (...) {}
    }
    for (const auto& m : match_set) matches.push_back(m);
  }
  if (match_index >= matches.size()) return nullptr;
  return strdup(matches[match_index++].c_str());
}

char** shell_completion(const char* text, int start, int end) {
    if (start == 0) {
        char** matches = rl_completion_matches(text, command_generator);
        if (matches == nullptr) { rl_ding(); }
        rl_attempted_completion_over = 1; 
        return matches;
    }
    return nullptr; 
}

// --- MAIN ---
int main() {
  rl_attempted_completion_function = shell_completion;

  while(true){
    char* input_cstr = readline("$ ");
    if (!input_cstr) break; 
    string input = input_cstr;
    if (!input.empty()){
        add_history(input_cstr);
        command_history.push_back(input);
    }
    free(input_cstr);

    // --- REDIRECTION LOGIC (MOVED TO TOP) ---
    // We handle redirection FIRST so that it applies to the last command in the pipeline automatically.
    int saved_stdout = dup(STDOUT_FILENO); 
    int saved_stderr = dup(STDERR_FILENO);
    bool redirectout = false, redirecterr = false, append_mode = false;
    string outfile;
    string clean_input = input; // Input stripped of redirection syntax
    
    // Scanner for redirection
    {
        bool in_quotes = false;
        char quote_char = 0;
        int redirect_pos = -1, redirect_len = 0;

        for (int i = 0; i < input.length(); i++) {
            if (input[i] == '\'' || input[i] == '"') {
                if (!in_quotes) { in_quotes = true; quote_char = input[i]; }
                else if (input[i] == quote_char) { in_quotes = false; }
            }
            if (!in_quotes && input[i] == '>') {
                redirect_pos = i; redirect_len = 1; redirectout = true;
                if (i + 1 < input.length() && input[i+1] == '>') { append_mode = true; redirect_len = 2; }
                if (i > 0 && input[i-1] == '1') { redirect_pos = i - 1; redirect_len++; }
                else if (i > 0 && input[i-1] == '2') { redirect_pos = i - 1; redirect_len++; redirecterr = true; redirectout = false; }
                break; 
            }
        }

        if (redirect_pos != -1) {
            clean_input = input.substr(0, redirect_pos);
            string raw_file = input.substr(redirect_pos + redirect_len);
            outfile = trim(raw_file);
        }

        if((redirectout || redirecterr) && !outfile.empty()) {
            int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
            int fd = open(outfile.c_str(), flags, 0644);
            if (fd >= 0) {
                if (redirectout) dup2(fd, STDOUT_FILENO);
                else dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
    }
    // --- END REDIRECTION ---

    // --- PIPELINE LOGIC (Multi-Stage) ---
    vector<string> commands = split_pipeline(clean_input);
    
    // Only engage pipeline logic if we actually have multiple segments
    if (commands.size() > 1) {
        int num_cmds = commands.size();
        int prev_pipe_read = -1; // Holds the read end of the previous pipe
        
        vector<pid_t> pids;

        for (int i = 0; i < num_cmds; i++) {
            int pipefd[2];
            
            // Create a pipe for everyone EXCEPT the last command
            if (i < num_cmds - 1) {
                if (pipe(pipefd) < 0) {
                    perror("pipe");
                    break;
                }
            }

            pid_t pid = fork();
            if (pid == 0) {
                // --- CHILD PROCESS ---
                
                // 1. Input Wiring: If not first, read from previous pipe
                if (prev_pipe_read != -1) {
                    dup2(prev_pipe_read, STDIN_FILENO);
                    close(prev_pipe_read);
                }

                // 2. Output Wiring: If not last, write to current pipe
                if (i < num_cmds - 1) {
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                
                // 3. Execution
                vector<string> args = parse_input(commands[i]);
                if (handle_builtin(args)) {
                    exit(0);
                }
                
                vector<char*> c_args;
                for(const auto& arg : args) c_args.push_back(strdup(arg.c_str()));
                c_args.push_back(nullptr);
                execvp(c_args[0], c_args.data());
                
                // If execvp fails
                exit(1);
            } else {
                // --- PARENT PROCESS ---
                pids.push_back(pid);

                // Close the PREVIOUS read end (we are done with it)
                if (prev_pipe_read != -1) {
                    close(prev_pipe_read);
                }

                // If this wasn't the last command, save the CURRENT read end for the next iteration
                // And close the write end (only the child needs that)
                if (i < num_cmds - 1) {
                    close(pipefd[1]);
                    prev_pipe_read = pipefd[0];
                }
            }
        }
        
        // Wait for all children to finish
        for(pid_t p : pids) {
            waitpid(p, nullptr, 0);
        }
        
    } else {
        // --- STANDARD EXECUTION (Single Command) ---
        // (This runs if there were no pipes)
        vector<string> args = parse_input(clean_input);
        if (!args.empty()) {
            if (!handle_builtin(args)) {
                string p = get_path(args[0]);
                if (!p.empty()) {
                    system(clean_input.c_str());
                } else {
                    cout << clean_input << ": command not found" << endl;
                }
            }
        }
    }

    // --- RESTORE FDS ---
    fflush(stdout); fflush(stderr);
    dup2(saved_stdout, STDOUT_FILENO); dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout); close(saved_stderr);
  }
}