#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <cstring> 
#include <vector>
#include <fcntl.h>

// Function definitions
int command_shell();
void print_return_status(int waitpid_status);
void change_dir(const char *path);
void push_dir(const char *curr_path, const char*new_path);
void pop_dir();
void dirstack_print();
void free_stack();
void free_command_memory(char *input_tmp, char *input, char **argv);
void perform_redirection(int redirection, char *redirect_file);

// Define directory stack
std::vector<const char*> stack;

int command_shell(){
  // Get current working directory
  char curr_path[MAXPATHLEN];
  if (getcwd(curr_path,MAXPATHLEN) != NULL){
    // Print prompt
    std::cout << "myShell:" << curr_path << " $";
  }
  else{
    perror("Issue attaining current directory");
    return -1;
  }

  // Read command line from stdin
  std::string command_line;
  std::getline(std::cin,command_line);

  // Check for EOF
  if (std::cin.bad()){
    // Free any memory in directory stack before exiting
    free_stack();
    return -1;
  }
  
  // Check for exit
  if (!command_line.compare("exit")){
    // Free any memory in directory stack before exiting
    free_stack();
    return -1;
  }

  // Check for blank input
  if (command_line.empty()){
    return 1;
  }


  // Parse the command_line in to program and arguments
  int input_len = command_line.size()+1;
  char *input = new char[input_len];
  int offset = 0;
  for (int i = 0; i<input_len-1; i++){
    // Check for escaped space, drop the space but keep \ for later use
    if ((command_line[i+offset] == ' ') && (command_line[i+offset-1] == '\\')){
      input[i] = command_line[i+offset+1];
      offset++;
      input_len--;
    }
    else{
      input[i] = command_line[i+offset];
    }
  }
  input[input_len-1] = '\0';


  // Split by whitespace
  std::vector<char*> args_vec;
  char *input_tmp = strdup(input);
  char *args;
  char *redirect_file_stdin = NULL;
  char *redirect_file_stdout = NULL;
  char *redirect_file_stderror = NULL;
  
  args = strtok(input_tmp," ");
  while (args != NULL){
    if (strcmp(args,"<") == 0){
      args = strtok(NULL," ");
      redirect_file_stdin = args;
    }
    else if (strcmp(args,">") == 0){
      args = strtok(NULL," ");
      redirect_file_stdout = args;
    }
    else if (strcmp(args,"2>") == 0){
      args = strtok(NULL," ");
      redirect_file_stderror = args;
    }
    else{
      int tmp_index = 0;
      while (args[tmp_index]){
	// Replace backslash with literal space
	if (args[tmp_index] == '\\') {
	  args[tmp_index] = ' ';
	}
	tmp_index++;
      }
      args_vec.push_back(args);
    }
    args = strtok(NULL," ");
  }

  // Get all arguments
  int args_len = args_vec.size()+1;
  char **argv = new char*[args_len];
  for (int i = 0; i<args_len-1; i++){
    argv[i] = args_vec[i];
  }
  argv[args_len-1] = NULL;

  // Program name is argument 0
  std::string program(args_vec[0]);

  // Change Directory (call to cd)
  if (!program.compare("cd")){
    change_dir(args_vec[1]);
    // Free memory
    free_command_memory(input_tmp,input,argv);
    return 1;
  }

  // Push directory to stack (call to pushd)
  if (!program.compare("pushd")){
    push_dir(curr_path,args_vec[1]);
    // Free memory
    free_command_memory(input_tmp,input,argv);
    return 1;
  }

  // Pop directory from stack (call to popd)
  if (!program.compare("popd")){
    pop_dir();
    // Free memory
    free_command_memory(input_tmp,input,argv);
    return 1;
  }

  // Print directory stack (call to dirstack)
  if (!program.compare("dirstack")){
    dirstack_print();
    // Free memory
    free_command_memory(input_tmp,input,argv);
    return 1;
  }
  
  // Search PATH environment variable for program if it does not contain a /
  if (program.find("/") == std::string::npos){
    char *path_all;
    bool found = false;
    path_all = getenv("PATH");
    // Make copy of environment path so strtok does not modify original
    char *path_all_tmp = strdup(path_all);
    if (path_all_tmp != NULL){
      char *path;
      path = strtok(path_all_tmp,":");
      while (path != NULL){
	std::string command(path);
	command = command + "/" + program;
	std::ifstream f(command);
	// Check to see if it exists in specified directory
	if (f.good()){
	  program = command;
	  found = true;
	  break;
	}
	path = strtok(NULL,":");
      }
    }
    free(path_all_tmp);
    if (found == false){
      std::cout << "Command " << program << " not found \n";
      // Free memory
      free_command_memory(input_tmp,input,argv);
      return 1;
    }
  }
  // Otherwise see if exists in absolute path
  else{
    std::ifstream fabs(program);
    if (!fabs.good()){
      std::cout << "Command " << program << " not found \n";
      // Free memory
      free_command_memory(input_tmp,input,argv);
      return 1;
    }
  }

  // Create Fork
  pid_t cpid = fork();
  if (cpid == -1){
    perror("Issue forking");
    free_stack();
    return -1;
  }
  
  // Execute Command
  else if (cpid == 0){

    // Input/Output Redirection
    if (redirect_file_stdin != NULL){
      perform_redirection(1,redirect_file_stdin);
    }
    if (redirect_file_stdout != NULL){
      perform_redirection(2,redirect_file_stdout);
    }
    if (redirect_file_stderror != NULL){
      perform_redirection(3,redirect_file_stderror);
    }

    const char *prog_command = program.c_str();
    char *environ[] = { NULL };
    execve(prog_command, (char * const *)argv, environ);
    perror("Issue executing requested command");
    return -1;
  }

  // Find out return status of executed process
  else{
    int waitpid_status;
    pid_t rpid = waitpid(cpid, &waitpid_status, 0);

    if (rpid == -1){
      perror("Issue waiting for forked process");
      return -1;
    }
    // Print exit or kill message
    print_return_status(waitpid_status);
  
    // Free memory
    free_command_memory(input_tmp,input,argv);

  }

  return 1;
}

// Print the return status
void print_return_status(int waitpid_status){
    if (WIFEXITED(waitpid_status)){
      std::cout << "Program exited with status " << WEXITSTATUS(waitpid_status) << "\n";
    }
    else if (WIFSIGNALED(waitpid_status)){
      std::cout << "Program was killed by signal " << WTERMSIG(waitpid_status) << "\n";
    }
}

// Change to specified directory
void change_dir(const char *path){
  int cd_sucess = chdir(path);
  if (cd_sucess == -1){
      perror("Issue with cd");
    }
}


// Push a directory to the stack and cd to new directory 
void push_dir(const char *curr_path, const char*new_path){
  const char *old_path = strdup(curr_path);
  stack.push_back(old_path);
  change_dir(new_path);
}

// Cd to the directory on top of stack and remove from directory stack
void pop_dir(){
  if (stack.empty()){
    std::cout << "Error using popd: The directory stack is empty. \n";
  }
  else{
    change_dir(stack.back());
    const char *path_to_free = stack.back();
    stack.pop_back();
    free((char*)path_to_free);
  }
}

// Print out contents of directory stack
void dirstack_print(){
  for(size_t i = 0; i < stack.size(); i++){
    std::cout << stack[i] << "\n";
  }
}

// Free memory associated with directory stack
void free_stack(){
  while (!stack.empty()){
    const char *path_to_free = stack.back();
    stack.pop_back();
    free((char*)path_to_free);
  }
}

// Free memory associated with command arguments
void free_command_memory(char *input_tmp, char *input, char **argv){
  free(input_tmp);
  delete[] input;
  delete[] argv;
}

// Redirect standard input/output/error
void perform_redirection(int redirection, char *redirect_file){
  // Std Input
  if (redirection == 1){
    int fd = open(redirect_file, O_RDONLY);
    if (fd < 0){
      perror("Issue Redirecting Standard Input");
    }
    dup2(fd,0);
    close(fd);
  }
  // Std Output
  else if (redirection == 2){
    int fd = open(redirect_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
    if (fd < 0){
      perror("Issue Redirecting Standard Output");
    }
    dup2(fd,1);
    close(fd);
  }
  // Std Error
  else if (redirection == 3){
    int fd = open(redirect_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
    if (fd < 0){
      perror("Issue Redirecting Standard Error");
    }
    dup2(fd,2);
    close(fd);
  }
}

int main(){

  while(true){
    int result = command_shell();
    if (result == -1){
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
