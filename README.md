# MyGit

[![C](https://img.shields.io/badge/language-C11-blue.svg)]()
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Status](https://img.shields.io/badge/status-active%20development-orange.svg)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)]()
[![Platform](https://img.shields.io/badge/platform-windows%20%7C%20linux-lightgrey.svg)]()

A minimal Git-like version control system written in C.

MyGit is an educational reimplementation of core version control concepts such as commits, staging, and history tracking. It is not intended to replace Git, but to demonstrate how such a system can be built from scratch.

---

## Features

- Repository initialization  
- File staging  
- Commit creation with messages  
- Commit history traversal  
- (WIP) Branching  
- (WIP) Diffing  

---

## Design

MyGit uses a simplified Git-inspired object model stored locally in `.mygit/`:

- **blobs** → file contents  
- **commits** → snapshots of repository state  
- **refs** → pointers to commits  
- **trees** → directory structure (planned / partial)  

There is no networking or remote support. Everything is local and intentionally minimal.

---

## Build

Use the included Makefile:

    make

Or manually:

    gcc -std=c11 -Wall -Wextra -Wpedantic -O0 -g -Iinclude src/*.c -o mygit.exe

---

## Usage

    ./mygit init
    ./mygit add file.txt
    ./mygit commit "message"
    ./mygit log

---

## Project Status

This project is actively developed.

Some features are complete, others are experimental, and some exist mostly as ideas waiting to become real bugs.

---

## Roadmap

- Linux support (currently untested / not configured)
- Improved diffing engine
- Full branching system
- Better internal object handling
- Reduced undefined behavior edge cases

---

## Contributing

Contributions are welcome.

Workflow:
- Fork the repository  
- Create a feature branch  
- Make focused changes  
- Ensure it builds  
- Open a pull request  

Guidelines:
- Keep changes small and readable  
- Avoid unnecessary complexity  
- If it feels over-engineered, it probably is  

---

## License

MIT — do whatever you want.

If it breaks, that’s part of the learning experience.
