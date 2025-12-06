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

int main() {
  while(true){
    cout << unitbuf;
    cerr << unitbuf;
    
    cout << "$ ";
    string input;
    getline(cin, input);

    // --- REDIRECTION LOGIC START ---
    int saved_stdout = dup(STDOUT_FILENO); 
    bool redirect = false;
    string outfile;
    string clean_input = input; // Default to full input if no redirect found

    bool in_quotes = false;
    char quote_char = 0;
    int redirect_pos = -1;
    int redirect_len = 0;

    // 1. Scan for unquoted '>'
    for (int i = 0; i < input.length(); i++) {
        // Toggle quote state
        if (input[i] == '\'' || input[i] == '"') {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = input[i];
            } else if (input[i] == quote_char) {
                in_quotes = false;
            }
        }
        
        // If we are NOT in quotes, checking for redirection
        if (!in_quotes) {
            if (input[i] == '>') {
                redirect_pos = i;
                redirect_len = 1;
                // Check if it is "1>" (Standard Output explicitly)
                if (i > 0 && input[i-1] == '1') {
                    redirect_pos = i - 1;
                    redirect_len = 2;
                }
                break; // Found the operator, stop scanning
            }
        }
    }

    // 2. If valid redirection found, split the string
    if (redirect_pos != -1) {
        redirect = true;
        
        // The command is everything BEFORE the operator
        clean_input = input.substr(0, redirect_pos);
        
        // The filename is everything AFTER the operator
        string raw_file = input.substr(redirect_pos + redirect_len);
        
        // Trim whitespace from the filename
        size_t first = raw_file.find_first_not_of(" \t");
        if(first != string::npos) {
            size_t last = raw_file.find_last_not_of(" \t");
            outfile = raw_file.substr(first, (last - first + 1));
        }
    }

    // 3. Perform the Redirection
    if(redirect && !outfile.empty()) {
        int fd = open(outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open");
        } else {
            dup2(fd, STDOUT_FILENO);
            close(fd);
            input = clean_input; // Update input for the next steps
        }
    }
    // --- REDIRECTION LOGIC END ---
    
    
    // Parse command and handle whitespace
    // (This uses the 'input' string which might have been modified above)
    stringstream iss(input);
    string command;
    iss >> command;

    if(command == "exit"){
      break;
    } else if(command=="echo"){
      if (input.length() > 5) {
          cout << input.substr(5) << endl;
      } else {
          cout << endl;
      }
    } else if(command=="type"){
      string arg;
      iss >> arg; 
      
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
      iss >> path; 
      if(chdir(path.c_str())==0 && path!="~"){
      } else if(path=="~"){
        const char* home = getenv("HOME");
        chdir(home);
      } else {
        cout << "cd: " << path << ": No such file or directory" << endl;
      }
    } else {
        // Run External Program
        string path = get_path(command);
        if (!path.empty()) {
          string remaining_args;
          getline(iss, remaining_args); 
          // Construct the full command
          string full_command = command + remaining_args;
          system(full_command.c_str());
        } else {
          cout<<input<<": command not found" << endl;
        }
    }

    // --- RESTORE STDOUT ---
    // Important: Flush buffers before swapping output back
    fflush(stdout); 
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
  };
}