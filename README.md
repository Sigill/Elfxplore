# Elfxplore

[Elfxplore](https://github.com/Sigill/Elfxplore) is a command line tool designed to explore the binary dependencies between all the binary artifacts composing a project.

## Database initialization

All the data extracted are stored in a SQLite database. Initialize it with the following command:

`# elfxplore db --init database.db`

## Dependencies analysis

__Purpose__: list the dependencies between source files, object files, librairies (static and shared) and executables.

The first step is to generate a list of all the compilation operations: calls to a compiler, linker or archiver.

The `cc-log`, `c++-log`, `ar-log` scripts are wrappers for those tools that will dump the executed command to the file referenced by the `OPLIST` environment variable (default: */tmp/operations.log*).

Note: Those wrappers use the `shell-quote` tool (usually provided by the *libstring-shellquote-perl* package).

The build environment must be configured to use those wrappers.

For Makefiles/autotools, set the following environment variables:

- CC: cc-log
- CXX: c++-log
- AR: ar-log

For CMake, set the following options:

- CMAKE_C_COMPILER: cc-log
- CMAKE_CXX_COMPILER: c++-log
- CMAKE_AR: ar-log

One the build is done, launch the analysis:

`# elfxplore analyse-dependencies -d database.db < /tmp/operations.log`

## Symbols analysis

__Purpose__: identify the symbols referenced in compilation artifacts (object files, librairies, executables).

`# elfxplore analyse-symbols -d database.db < symbols.txt`

## License

This tool is released under the terms of the MIT License. See the LICENSE.txt file for more details.
