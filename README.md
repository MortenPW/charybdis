# IRCd

**I**nternet **R**elay **C**hat **d**aemon: *Charybdis*

Charybdis is designed to be fast and highly scalable. It is also designed to be community-
developed by volunteer contributors over the internet. This makes Charybdis easy to understand,
modify, audit, and extend.

IRCd is a free and open source server which facilitates real-time communication over the
internet. It was started in 1988 by Jarkko Oikarinen in the University of Oulu and eventually
made its way to William Pitcock et al, whom after 2005 developed the project under the alias
*Charybdis*. In 2014 a protocol was proposed to reinvigorate real-time communication in lieu
of growing commercial competition and a lack of innovation from open source alternatives to
compete. This protcol is known as the *Matrix* protocol.

##### IRCd now implements the Matrix protocol.


## Charybdis/5

Charybdis Five is the first high performance implementation of *Matrix* written in C++. It remains
true to its roots for being highly scalable, modular and having minimal requirements. Most of the
old code has been rewritten but the architecture is the same.


### Dependencies

	* **Boost** (1.61 or later) - We have replaced libratbox with the well known and actively
	developed Boost libraries. These are included as a submodule in this repository.

	* **RocksDB** (based on LevelDB) - We replace sqlite3 with a lightweight and embedded database
	and have furthered the mission of eliminating the need for external "IRC services"


## Building from git (DEVELOPER PREVIEW INSTRUCTIONS)

	The developer preview will install charybdis in a specific directory isolated from the
	system. It will not use or install system libraries. It will download and build the
	dependencies from the submodules we have pinned here and build them the way we have
	configured. Charybdis should be executed using those builds. You may need to set the
	`LD_LIBRARY_PATH` to the built libraries. None of this will be required when released.

	* `git clone https://github.com/charybdis-ircd/charybdis`
	* `cd charybdis`
	* `git checkout 5`
		- Verify you have the latest source tree and **are on the Matrix branch**.

	* `./autogen.sh`
	* `mkdir build`
		- The install directory may be this or another place of your choosing.
		- If you decide elsewhere, make sure to change the `--prefix` in the `./configure`
		statement below.

	* `CXX=g++-6 ./configure --prefix=$PWD/build --enable-debug --with-included-boost=shared --with-included-rocksdb=shared`
		- Many systems alias `g++` to an older version. To be safe, specify a version manually
		in `CXX`. This will also build the submodule dependencies with that version.
		- The `--with-included-*` will fetch, configure **and build** the dependencies included
		as submodules. Include `=shared` for now until things are changed around.

	* `make`
	* `make install`


## Platforms

[![Charybdis](http://img.shields.io/SemVer/v5.0.0-dev.png)](https://github.com/charybdis-ircd/charybdis/tree/master)
*This branch is not meant for production. Use at your own risk.*

| <sub> Continuously Integrated Host </sub>   | <sub> Compiler </sub>    | <sub> Third party </sub> | <sub> Status </sub> |
|:------------------------------------------- |:------------------------ |:------------------------ |:------------------- |
| <sub> Linux Ubuntu 12.04 Precise </sub>     | <sub> GCC 5       </sub> | <sub> Boost 1.61 </sub>  | [![POSIX Build Status](https://travis-ci.org/charybdis-ircd/charybdis.svg?branch=master)](https://travis-ci.org/charybdis-ircd/charybdis) |
| <sub> Linux Ubuntu 14.04 Trusty </sub>      | <sub> GCC 6       </sub> | <sub> Boost 1.61 </sub>  | [![POSIX Build Status](https://travis-ci.org/charybdis-ircd/charybdis.svg?branch=master)](https://travis-ci.org/charybdis-ircd/charybdis) |
| <sub> Linux Ubuntu 14.04 Trusty </sub>      | <sub> Clang 3.8   </sub> | <sub> Boost 1.61 </sub>  | [![POSIX Build Status](https://travis-ci.org/charybdis-ircd/charybdis.svg?branch=master)](https://travis-ci.org/charybdis-ircd/charybdis) |
| <sub> Apple Darwin 15.5 </sub>              | <sub> LLVM 7.3.0  </sub> | <sub> Boost 1.61 </sub>  | [![POSIX Build Status](https://travis-ci.org/charybdis-ircd/charybdis.svg?branch=master)](https://travis-ci.org/charybdis-ircd/charybdis) |
| <sub> Windows </sub>                        | <sub> mingw 3.5   </sub> | <sub> Boost 1.61 </sub>  | [![Windows Build Status](https://ci.appveyor.com/api/projects/status/is0obsml8xyq2qk7/branch/master?svg=true)](https://ci.appveyor.com/project/kaniini/charybdis/branch/master) |

## Tips

 * Please read doc/index.txt to get an overview of the current documentation.

 * Read the NEWS file for what's new in this release.

## Developers

### Style

#### Misc

	* When using a `switch` over an `enum` type, put what would be the `default` case after/outside
	of the `switch` unless the situation specifically calls for one. We use -Wswitch so changes to
	the enum will provide a good warning to update any `switch`.

	* Prototypes should name their argument variables to make them easier to understand, except if
	such a name is redundant because the type carries enough information to make it obvious. In
	other words, if you have a prototype like `foo(const std::string &message)` you should name
	`message` because std::string is common and *what* the string is for is otherwise opaque.
	OTOH, if you have `foo(const options &, const std::string &message)` one should skip the name
	for `options &` as it just adds redundant text to the prototype.
