# Project: mynetcat

## Description
This project is a network communication tool that allows input and output redirection between different processes using various types of sockets (TCP, UDP, and Unix Domain Sockets). The tool supports executing programs, redirecting their input and output to different connections, and handling socket communication between processes.

The main feature of the project includes a simple Tic-Tac-Toe game (TTT) where the program makes moves based on a predefined strategy. The program's decision-making process is based on a 9-digit number that prioritizes board positions according to the number's significance.

## Features

### Stage 1: Tic-Tac-Toe Game
Implemented a simple algorithm for playing Tic-Tac-Toe. The program makes decisions based on a given strategy number, where the most significant digit (MSD) determines the first move and the least significant digit (LSD) determines the last available move.

### Stage 2: mynetcat Tool
Developed a utility `mync` that supports the `-e` parameter to execute a program (e.g., "ttt 123456789") and redirect its input/output to various socket types (TCP, UDP). 

### Stage 3: TCP and UDP Communication
The tool supports input and output redirection via TCP and UDP sockets in both server and client modes. This allows for communication between processes running on different machines or within the same system.

### Stage 4: Timeout Mechanism
Implemented a timeout feature that uses the `alarm(2)` function to terminate the process after a specified number of seconds if there is no activity.

### Stage 5: MUX Support
Developed support for handling multiple client connections using TCP multiplexing (MUX), allowing the server to handle more than one client simultaneously.

### Stage 6: Unix Domain Sockets
Added support for Unix Domain Sockets, both for datagram and stream communication, allowing local communication between processes on the same machine.

## Setup and Usage

1. Clone the repository:
git clone <repository-url>

markdown
Copy code

2. Compile the code using `make`:
make

csharp
Copy code

3. Run the program with a strategy for Tic-Tac-Toe:
mync -e "ttt 123456789" -i TCPS4050

markdown
Copy code

### Available Parameters:
- `-e`: Execute the given program (e.g., `mync -e "ttt 123456789"`).
- `-i`: Input redirection from the specified socket (e.g., TCP, UDP).
- `-o`: Output redirection to the specified socket.
- `-b`: Both input and output redirection.
- `-t`: Timeout in seconds to automatically terminate the program if idle.

## Notes

- For UDP and Unix Domain Sockets, certain functionality like output redirection or input reception is restricted based on the protocol.
- If no `-e` parameter is provided, input and output are handled via standard input and output, allowing for communication between two `mync` instances.

## Code Coverage

The project includes code coverage reports, which can be found in the `coverage` directory.

## Conclusion
This tool facilitates communication between processes over different socket types while also offering the functionality of executing and interacting with external programs. The Tic-Tac-Toe game provides a basic example of the redirection capabilities in use.
