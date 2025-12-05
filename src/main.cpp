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
    }
    cout<<input<<": command not found" << endl;
  };
}
