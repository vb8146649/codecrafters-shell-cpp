#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> // Required for getenv, system
#include <unistd.h> // Required for access() and X_OK
#include <filesystem>

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
  // Flush after every std::cout / std:cerr
  while(true){
    cout << unitbuf;
    cerr << unitbuf;
    
    cout << "$ ";
    string input;
    getline(cin, input);
    
    // Parse command and handle whitespace
    stringstream iss(input);
    string command;
    iss >> command;

    if(command == "exit"){
      break;
    } else if(command=="echo"){
      // Use basic substring to handle echo arguments (simplistic approach)
      if (input.length() > 5) {
          cout << input.substr(5) << endl;
      } else {
          cout << endl;
      }
      continue;
    } else if(command=="type"){
      string arg;
      iss >> arg; // Get the next word only
      
      // 1. Check Builtins
      if(arg == "echo" || arg == "exit" || arg == "type" || arg == "pwd") {
        cout << arg << " is a shell builtin" << endl;
      } 
      // 2. Check PATH
      else {
        string path = get_path(arg);
        if (!path.empty()) {
          cout << arg << " is " << path << endl;
        } else {
          cout << arg << ": not found" << endl;
        }
      }
      continue;
    }else if(command=="pwd"){
      cout<<filesystem::current_path().string()<<endl;
      continue;
    }

    // 3. Run External Program
    string path = get_path(command);
    if (!path.empty()) {
      // Get the rest of the arguments from the stringstream
      string remaining_args;
      getline(iss, remaining_args); 
      
      // Construct the full command: absolute path + arguments
      string full_command = command + remaining_args;
      // Execute
      system(full_command.c_str());
      continue;
    }
    
    // Fallback for unknown commands
    cout<<input<<": command not found" << endl;
  };
}