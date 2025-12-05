#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> // Required for getenv
#include <unistd.h> // Required for access() and X_OK

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
    
    if(input == "exit 0"){ // Adjusted to handle "exit 0" typically required by tests
      break;
    } else if(input == "exit") {
      break;
    } else if(input.substr(0, 5) == "echo "){
      cout<<input.substr(5)<<endl;
      continue;
    } else if(input.substr(0, 5)=="type "){
      string command = input.substr(5);
      
      // 1. Check Builtins
      if(command == "echo" || command == "exit" || command == "type"){
        cout << command << " is a shell builtin" << endl;
      } 
      // 2. Check PATH
      else {
        string path = get_path(command);
        if (!path.empty()) {
          cout << command << " is " << path << endl;
        } else {
          cout << command << ": not found" << endl;
        }
      }
      continue;
    }
    
    // Fallback for unknown commands entered directly
    cout<<input<<": command not found" << endl;
  };
}