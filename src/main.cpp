#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> 
#include <unistd.h> 
#include <filesystem>
#include <fcntl.h> // Required for open(), O_CREAT, etc.

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
            if (in_double) {
                current_arg += c; 
            } else {
                in_single = !in_single; 
            }
        } else if (c == '"') {
            if (in_single) {
                current_arg += c; 
            } else {
                in_double = !in_double; 
            }
        } else if (c == ' ') {
            if (in_single || in_double) {
                current_arg += c; 
            } else if (!current_arg.empty()) {
                args.push_back(current_arg); 
                current_arg = "";
            }
        } else {
            current_arg += c; 
        }
    }
    
    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }
    
    return args;
}

int main() {
  while(true){
    cout << unitbuf;
    cerr << unitbuf;
    
    cout << "$ ";
    string input;
    getline(cin, input);

    // --- REDIRECTION LOGIC START ---
    int saved_stdout = dup(STDOUT_FILENO); 
    int saved_stderr = dup(STDERR_FILENO);
    
    // Variables to track the redirection state
    bool redirect = false;
    bool append_mode = false; 
    int target_fd = STDOUT_FILENO; // Default to stdout (1)
    
    string outfile;
    string clean_input = input; 

    bool in_quotes = false;
    char quote_char = 0;
    int redirect_pos = -1;
    int redirect_len = 0;

    // Scan for operators
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
                target_fd = STDOUT_FILENO; // Default
                append_mode = false;       // Default Truncate

                // Check for Append (>>)
                if (i + 1 < input.length() && input[i+1] == '>') {
                    append_mode = true;
                    redirect_len = 2; // Operator is 2 chars long
                }

                // Check for FD specification (1> or 2> or 1>> or 2>>)
                if (i > 0) {
                    if (input[i-1] == '1') {
                        redirect_pos = i - 1;
                        redirect_len++; // Add 1 to length for the digit
                        target_fd = STDOUT_FILENO;
                    } else if (input[i-1] == '2') {
                        redirect_pos = i - 1;
                        redirect_len++; 
                        target_fd = STDERR_FILENO;
                    }
                }
                break; // Stop scanning once found
            }
        }
    }

    // Split string if redirection found
    if (redirect_pos != -1) {
        redirect = true;
        clean_input = input.substr(0, redirect_pos);
        string raw_file = input.substr(redirect_pos + redirect_len);
        
        size_t first = raw_file.find_first_not_of(" \t");
        if(first != string::npos) {
            size_t last = raw_file.find_last_not_of(" \t");
            outfile = raw_file.substr(first, (last - first + 1));
        }
    }

    // Perform the Redirection
    if(redirect && !outfile.empty()) {
        int flags = O_WRONLY | O_CREAT;
        
        // Choose between Append and Truncate
        if (append_mode) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }

        int fd = open(outfile.c_str(), flags, 0644);
        if (fd < 0) {
            perror("open");
        } else {
            dup2(fd, target_fd);
            close(fd);
            input = clean_input; 
        }
    }
    // --- REDIRECTION LOGIC END ---
    
    // Parse cleaned input
    vector<string> args = parse_input(clean_input);
    if (args.empty()) {
         // Restore and continue if line was empty
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
          // Use input (which is clean_input) directly to preserve quotes for external args
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