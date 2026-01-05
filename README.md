# Distributed File System

Stores files by breaking them up across multiple servers with some redundancy, and gets and reconstructs them when needed. Built to handle server failure, still being able to reconstruct files from other data on other servers.

Built for use on Linux machines

---

### Usage:
- run `make` to build runnables
- run `make clean` to delete runnables

#### Server:
- './dfs <storage directory> <port>

#### Client:
- `./dfc <command> [parameter]
- Must have a config file (`dfc.conf`) in the user's `$HOME` directory. Each line of the file defines another server the client can use, following the format of `server <server name> <ip:port>`. Server names are arbitrary
- List reconstructable files on servers: `./dfc list`
- Put file: `./dfc put <filepath>`
- Get file: `/dfc get <filename>`

---

> April 2025
