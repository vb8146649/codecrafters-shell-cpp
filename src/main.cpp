#include <iostream>
#include <string>
using namespace std;

int main() {
  // Flush after every std::cout / std:cerr
  while(true){
    cout << unitbuf;
    cerr << unitbuf;
    
    // TODO: Uncomment the code below to pass the first stage
    cout << "$ ";
    string input;
    getline(cin, input);
    if(input == "exit"){
      break;
    }else if(input.substr(0, 5) == "echo "){
      cout<<input.substr(5)<<endl;
      continue;
    }else if(input.substr(0, 5)=="type "){
      string command = input.substr(5);
      if(command == "echo"){
        cout<<"echo is a shell builtin"<<endl;
      }else if(command == "type"){
        cout<<"type is a shell builtin"<<endl;
      }else if(command == "exit"){
        cout<<"exit is a shell builtin"<<endl;
      }else{
        cout<<command<<": not found"<<endl;
      }
      continue;
    }
    cout<<input<<": command not found" << endl;
  };
}
