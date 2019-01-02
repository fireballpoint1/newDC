# Installing and Compiling uhub
This provides specific [installation](https://www.uhub.org/doc/compile.php) and [configuration](https://www.uhub.org/doc/) directions in a single place for our purposes (Not satisfying any general-case situations, all such is covered in the official documentation in parts).

Assumption: the software is being run on a Linux or a Linux-like system. Till date it has only been tested on Ubuntu.

## Installation
This section will be covering installing from source. If you're on Ubuntu you can skip this section after running `sudo apt install uhub`

### Prerequisites
Before compiling try to ensure the following is satisfied:
- CMake (2.8.3+)
- GCC, Clang, MinGW, Cygwin or Visual Studio 2010.
- OpenSSL 0.9.8+ (optional)
- SQLite3 (optional)

To install all of these dependencies, run

```
sudo apt-get install cmake make gcc git libsqlite3-dev libssl-dev
```

Now, clone the repo. Use [the original repo](https://github.com/janvidar/uhub) or [this fork](https://github.com/zubairabid/uhub) as you wish:
```
git clone https://github.com/zubairabid/uhub.git
```

### Compiling
`cd` into the uhub directory and the following commands should do fine:
```
cmake .
make
make install
```

## Configuration
When configuring uhub keep in mind the default directories
|            Dir           |             Path               |
|:------------------------:|:------------------------------:|
|        Binaries          |       /usr/local/bin/          |
|   Configuration Files    |          /etc/uhub/            |
|         Plugins          |        /usr/lib/uhub/          |

### Configuration files
The configuration files are available [here](./uhubconf). Add the files to the respective directories

## Running uhub
`sudo uhub` inside the uhub repo should do


