# COSE322 Optional Project

## Overview

This repository contains a mini shell implementation developed as part of the COSE322 System Programming optional project. The purpose of this project is to demonstrate fundamental concepts of operating systems and system programming through a simplified command-line interpreter.

The shell accepts user commands, creates child processes, executes Linux programs, and manages process synchronization. Through this implementation, several core concepts from the COSE322 course are explored, including process creation, process management, system calls, and Linux command execution.

## Features

* Interactive command-line interface
* User command parsing
* Process creation using fork()
* Program execution using the exec() family of system calls
* Parent-child process synchronization using wait()/waitpid()
* Execution of common Linux commands such as ls, pwd, and ps
* Multi-command pipelines using pipe()
* Input redirection using `<`
* Output redirection using `>` and `>>`
* Built-in cd, exit, and quit commands
* Continuous command loop until termination

## System Programming Concepts

## Processes

The shell creates a new child process whenever a command is executed. This demonstrates process creation and management in Unix-like operating systems.

## System Calls

The project utilizes several Linux system calls, including:

* fork()
* execvp()
* waitpid()
* pipe()
* dup2()
* open()
* close()

These system calls provide communication between user-level applications and the operating system kernel.

## Linux Command Execution

The shell allows users to execute standard Linux commands. Command execution is performed in child processes, similar to the behavior of real Unix shells.

## Process Synchronization

The parent process waits for child processes to complete before returning control to the command prompt. This ensures proper execution order and resource management.

## Pipes

The shell supports pipelines such as:

```sh
ls | grep txt
ps aux | grep bash | wc -l
```

For each pipe, the shell creates a communication channel with pipe(), redirects the standard output of one child process to the pipe, and redirects the standard input of the next child process from the pipe.

## Input and Output Redirection

The shell supports basic file redirection:

```sh
ls > output.txt
echo hello >> output.txt
wc -l < output.txt
```

Input redirection opens a file and connects it to standard input. Output redirection opens or creates a file and connects it to standard output. The shell uses dup2() to replace the child process file descriptors before running execvp().

## Build and Run

Compile the program with:

```sh
gcc -Wall -Wextra -std=c11 -o mini_shell system_notes_demo.c
```

Run the shell with:

```sh
./mini_shell
```

Exit the shell with:

```sh
exit
```

## Program Flow

1. Display the shell prompt and wait for user input.
2. Read and parse the command entered by the user.
3. Identify command arguments, pipes, and redirection operators.
4. Create pipes when the command line contains |.
5. Create child processes using fork().
6. Connect child process input and output using dup2().
7. Execute the requested command in each child process using execvp().
8. The parent process waits for child processes using waitpid().
9. Display the prompt again and continue execution.
