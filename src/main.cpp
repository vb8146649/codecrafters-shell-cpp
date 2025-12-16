/*
 * CppShell - A POSIX-compliant shell built from scratch.
 * * This program implements a command-line interpreter that interacts directly 
 * with the Linux kernel using raw system calls. It avoids high-level abstractions 
 * like system() to demonstrate manual process management, memory handling, and 
 * file descriptor manipulation.
 *
 * Key Features Implemented Manually:
 * - Process Creation: fork(), execvp(), waitpid()
 * - Inter-Process Communication: pipe(), dup2()
 * - Signal Handling & I/O: Custom tokenizer and redirection logic
 */

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib> 
#include <unistd.h>      // For fork, execvp, pipe, dup2
#include <filesystem>
#include <fcntl.h>       // For open flags (O_CREAT, O_WRONLY, etc.)
#include <cstring> 
#include <readline/readline.h> 
#include <readline/history.h>  
#include <set>
#include <algorithm>
#include <sys/wait.h>    // For waitpid
#include <fstream>

using namespace std;

// --- Globals ---
// List of internal commands handled directly by the shell process
const vector<string> builtins = {"exit", "echo", "type", "pwd", "cd", "history"};

// In-memory storage for command history
vector<string> command_history;

// Tracks which history entries have already been written to disk (for 'history -a')
int history_write_index = 0; 

// --- Helper Functions ---

/**
 * Searches the system PATH for the given executable name.
 * Returns the absolute path if found, or an empty string.
 */
string get_path(string command) {
  string path_env = std::getenv("PATH");
  if (!path_env.empty()) {
        stringstream ss(path_env);
        string path;
        // Split PATH by ':' and check each directory
        while (getline(ss, path, ':')) {
            string abs_path = path + "/" + command;
            // X_OK checks if the file exists and is executable
            if (access(abs_path.c_str(), X_OK) == 0) return abs_path;
        }
  }
  return "";
}

/**
 * Trims leading and trailing whitespace from a string.
 */
string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// --- CORE PARSER ---

/**
 * Parses a raw input string into arguments.
 * Implements a state machine to handle:
 * 1. Single Quotes ('): Literal interpretation (no escaping).
 * 2. Double Quotes ("): Selective escaping ($, \, ", newline).
 * 3. Backslashes (\): Escape characters based on the quote state.
 */
vector<string> parse_input(string input) {
    vector<string> args;
    string current_arg;
    bool in_single = false;
    bool in_double = false;
    
    for (int i = 0; i < input.length(); i++) {
        char c = input[i];
        
        // --- Backslash Escaping Logic ---
        if (c == '\\') {
            if (in_single) {
                // Inside single quotes, backslash is a literal character.
                current_arg += c;
            } 
            else if (in_double) {
                // Inside double quotes, only escape specific characters.
                if (i + 1 < input.length()) {
                    char next = input[i+1];
                    if (next == '"' || next == '\\' || next == '$' || next == '\n') {
                        current_arg += next; 
                        i++; // Skip the escaped character
                    } else {
                        current_arg += c; // Treat backslash as literal
                    }
                } else {
                    current_arg += c; // Trailing backslash
                }
            } 
            else {
                // Outside quotes, backslash escapes everything (e.g., spaces).
                if (i + 1 < input.length()) {
                    current_arg += input[i+1];
                    i++;
                }
            }
            continue; // Move to next character
        }

        // --- Quote State Toggling ---
        if (c == '\'') {
            if (in_double) current_arg += c; // Literal single quote inside double quotes
            else in_single = !in_single;     // Toggle single quote state
        } else if (c == '"') {
            if (in_single) current_arg += c; // Literal double quote inside single quotes
            else in_double = !in_double;     // Toggle double quote state
        } else if (c == ' ') {
            // Space is a delimiter only if NOT inside quotes
            if (in_single || in_double) current_arg += c; 
            else if (!current_arg.empty()) {
                args.push_back(current_arg); 
                current_arg = "";
            }
        } else {
            current_arg += c; 
        }
    }
    // Push the final argument if exists
    if (!current_arg.empty()) args.push_back(current_arg);
    return args;
}

/**
 * Splits the input string by pipes ('|') while respecting quotes.
 * Ensures that a pipe symbol inside quotes (e.g., "echo '|'") is treated 
 * as text, not a pipeline separator.
 */
vector<string> split_pipeline(string input) {
    vector<string> commands;
    bool in_quotes = false;
    char quote_char = 0;
    int start = 0;
    
    for (int i = 0; i < input.length(); i++) {
        // Skip escaped characters (unless inside single quotes)
        if (input[i] == '\\') {
            if (!(in_quotes && quote_char == '\'')) {
                i++; 
                continue;
            }
        }
        
        // Track quote state
        if (input[i] == '\'' || input[i] == '"') {
            if (!in_quotes) { in_quotes = true; quote_char = input[i]; } 
            else if (input[i] == quote_char) { in_quotes = false; }
        }
        
        // Split only if we find a pipe OUTSIDE of quotes
        if (!in_quotes && input[i] == '|') {
            commands.push_back(trim(input.substr(start, i - start)));
            start = i + 1;
        }
    }
    commands.push_back(trim(input.substr(start)));
    return commands;
}

// --- HISTORY HELPERS ---

void save_history_to_file() {
    char* histfile_env = getenv("HISTFILE");
    if (histfile_env) {
        ofstream history_file(histfile_env); 
        if (history_file.is_open()) {
            for (const auto& cmd : command_history) {
                history_file << cmd << endl;
            }
            history_file.close();
        }
    }
}

// --- BUILTIN COMMAND HANDLER ---

/**
 * Executes built-in commands directly within the shell process.
 * Returns true if a builtin was executed, false otherwise.
 */
bool handle_builtin(const vector<string>& args) {
    if (args.empty()) return false;
    string command = args[0];

    if (command == "exit") {
        save_history_to_file();
        exit(0); // Terminate the shell
    } 
    else if (command == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            cout << args[i] << (i < args.size() - 1 ? " " : "");
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
            if (chdir(p.c_str()) != 0) cout << "cd: " << p << ": No such file or directory" << endl;
        }
        return true;
    }
    else if(command == "history") {
        // history -r: Read from file
        if(args.size() > 1 && args[1] == "-r") { 
            if(args.size() < 3) { cout << "history: -r needs arg" << endl; return true; }
            ifstream hf(args[2]);
            if (!hf.is_open()) { cout << "history: " << args[2] << ": No such file" << endl; return true; }
            string line;
            int lines_read = 0;
            while (getline(hf, line)) { if(!line.empty()) { command_history.push_back(line); lines_read++; } }
            history_write_index += lines_read;
            hf.close();
            return true;
        }
        // history -w: Write (overwrite) to file
        if(args.size() > 1 && args[1] == "-w") { 
            if(args.size() < 3) { cout << "history: -w needs arg" << endl; return true; }
            ofstream hf(args[2]);
            if (!hf.is_open()) { cout << "history: " << args[2] << ": Error" << endl; return true; }
            for (const auto& cmd : command_history) hf << cmd << endl;
            hf.close();
            return true;
        }
        // history -a: Append new commands to file
        if(args.size() > 1 && args[1] == "-a") { 
            if(args.size() < 3) { cout << "history: -a needs arg" << endl; return true; }
            ofstream hf(args[2], ios::app);
            if (!hf.is_open()) { cout << "history: " << args[2] << ": Error" << endl; return true; }
            for (int i = history_write_index; i < command_history.size(); i++) hf << command_history[i] << endl;
            history_write_index = command_history.size();
            hf.close();
            return true;
        }
        // history -c: Clear history
        if(args.size() > 1 && args[1] == "-c") { command_history.clear(); history_write_index = 0; return true; }
        
        // Default: Print history
        size_t start_index = 0; 
        if(args.size() > 1) { try { int n = stoi(args[1]); if (n < command_history.size()) start_index = command_history.size() - n; } catch (...) {} }
        for (size_t i = start_index; i < command_history.size(); ++i) cout << "    " << (i + 1) << "  " << command_history[i] << endl;
        return true;
    }
    return false;
}

// --- AUTOCOMPLETE LOGIC ---
// Integration with libreadline to provide tab-completion for builtins and PATH executables.

char* command_generator(const char* text, int state) {
  static vector<string> matches;
  static size_t match_index = 0;
  if (state == 0) {
    matches.clear(); match_index = 0; string query = text; set<string> match_set;
    
    // 1. Check builtins
    for(const auto& cmd : builtins) if(cmd.find(query) == 0) match_set.insert(cmd);
    
    // 2. Check executables in PATH
    string path_env = std::getenv("PATH");
    if (path_env.data()) {
        stringstream ss(path_env); string path;
        while (getline(ss, path, ':')) {
            if (!filesystem::exists(path)) continue;
            try { for (const auto& entry : filesystem::directory_iterator(path)) {
                    string fn = entry.path().filename().string();
                    if (fn.find(query) == 0 && access(entry.path().c_str(), X_OK) == 0) match_set.insert(fn);
            }} catch (...) {}
        }
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

// --- MAIN LOOP ---
int main() {
  // Hook up the autocomplete function
  rl_attempted_completion_function = shell_completion;

  // 1. Startup: Load Persistent History
  // We check the HISTFILE environment variable to load previous sessions.
  char* histfile_env = getenv("HISTFILE");
  if (histfile_env) {
      ifstream history_file(histfile_env);
      if (history_file.is_open()) {
          string line;
          while (getline(history_file, line)) {
              if (!line.empty()) {
                  command_history.push_back(line);
              }
          }
          history_file.close();
          // Sync index so 'history -a' only appends *new* commands
          history_write_index = command_history.size();
      }
  }

  // 2. The REPL (Read-Eval-Print Loop)
  while(true){
    // Read input (libreadline handles line editing and arrow keys)
    char* input_cstr = readline("$ ");
    
    // Handle Ctrl+D (EOF) gracefully
    if (!input_cstr) {
        save_history_to_file(); 
        break; 
    }

    string input = input_cstr;
    if (!input.empty()){
        add_history(input_cstr); // Add to readline's internal history (up arrow)
        command_history.push_back(input); // Add to our persistent history vector
    }
    free(input_cstr);

    // 3. I/O Redirection Scanner
    // We scan for '>' or '>>' to setup file descriptors *before* execution.
    // Standard File Descriptors: 0 (STDIN), 1 (STDOUT), 2 (STDERR)
    int saved_stdout = dup(STDOUT_FILENO); 
    int saved_stderr = dup(STDERR_FILENO);
    bool redirectout = false, redirecterr = false, append_mode = false;
    string outfile;
    string clean_input = input; 

    {
        bool in_quotes = false; 
        char quote_char = 0; 
        int redirect_pos = -1, redirect_len = 0;
        
        // Scan specifically for unquoted redirection symbols
        for (int i = 0; i < input.length(); i++) {
            if (input[i] == '\\') {
                if (!(in_quotes && quote_char == '\'')) { i++; continue; }
            }
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
            clean_input = input.substr(0, redirect_pos); // Command part
            string raw_file = input.substr(redirect_pos + redirect_len); // File part
            
            // Clean the filename (remove quotes, handle escapes)
            vector<string> processed_file = parse_input(trim(raw_file));
            if (!processed_file.empty()) {
                outfile = processed_file[0];
            }
        }
        
        // Apply Redirection using dup2()
        if((redirectout || redirecterr) && !outfile.empty()) {
            int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
            int fd = open(outfile.c_str(), flags, 0644);
            if (fd >= 0) { 
                if (redirectout) dup2(fd, STDOUT_FILENO); // Replace STDOUT with file
                else dup2(fd, STDERR_FILENO);             // Replace STDERR with file
                close(fd); 
            }
        }
    }

    // 4. Pipeline Processing
    // We split the command by '|' and create a chain of processes.
    vector<string> commands = split_pipeline(clean_input);
    
    if (commands.size() > 1) {
        // --- MULTIPLE COMMANDS (PIPELINE) ---
        int num_cmds = commands.size(); 
        int prev_pipe_read = -1; // "The Baton": Holds the read-end from the previous command
        vector<pid_t> pids;

        for (int i = 0; i < num_cmds; i++) {
            int pipefd[2];
            // Create a pipe for everyone except the last command
            if (i < num_cmds - 1) { if (pipe(pipefd) < 0) break; }
            
            pid_t pid = fork(); // Clone the process
            
            if (pid == 0) {
                // --- CHILD PROCESS ---
                
                // 1. Input Wiring: Read from previous pipe (if exists)
                if (prev_pipe_read != -1) { 
                    dup2(prev_pipe_read, STDIN_FILENO); 
                    close(prev_pipe_read); 
                }
                
                // 2. Output Wiring: Write to current pipe (if not last cmd)
                if (i < num_cmds - 1) { 
                    dup2(pipefd[1], STDOUT_FILENO); 
                    close(pipefd[0]); 
                    close(pipefd[1]); 
                }
                
                // 3. Execution
                vector<string> args = parse_input(commands[i]);
                if (handle_builtin(args)) exit(0);
                
                // Convert string args to C-style array for execvp
                vector<char*> c_args; 
                for(const auto& arg : args) c_args.push_back(strdup(arg.c_str())); 
                c_args.push_back(nullptr);
                
                // Replace child memory with new program
                execvp(c_args[0], c_args.data()); 
                exit(1); // Only reached if execvp fails
            } else {
                // --- PARENT PROCESS ---
                pids.push_back(pid);
                
                // Cleanup: Parent doesn't need the previous read-end anymore
                if (prev_pipe_read != -1) close(prev_pipe_read);
                
                // Save the current read-end for the NEXT iteration
                if (i < num_cmds - 1) { 
                    close(pipefd[1]); // Close write-end (only child writes)
                    prev_pipe_read = pipefd[0]; 
                }
            }
        }
        // Wait for all children to finish to avoid zombies
        for(pid_t p : pids) waitpid(p, nullptr, 0);

    } else {
        // --- SINGLE COMMAND EXECUTION ---
        vector<string> args = parse_input(clean_input);
        
        if (!args.empty()) {
            if (!handle_builtin(args)) {
                string p = get_path(args[0]);
                
                if (!p.empty()) {
                    // Use fork/execvp for single commands too.
                    // This handles quoted executables (e.g., 'my program') correctly.
                    pid_t pid = fork();
                    if (pid == 0) {
                        vector<char*> c_args;
                        for(const auto& arg : args) c_args.push_back(strdup(arg.c_str()));
                        c_args.push_back(nullptr);
                        execvp(args[0].c_str(), c_args.data());
                        perror("execvp");
                        exit(1);
                    } else {
                        waitpid(pid, nullptr, 0);
                    }
                } else {
                    cout << args[0] << ": command not found" << endl;
                }
            }
        }
    }

    // 5. Cleanup
    // Restore standard I/O (in case it was redirected)
    fflush(stdout); fflush(stderr);
    dup2(saved_stdout, STDOUT_FILENO); dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout); close(saved_stderr);
  }
}