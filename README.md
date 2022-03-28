# sendwol &ndash; a simple Wake-on-LAN client

## Usage

For example, most simply:

```console
% sendwol 00:00:5e:00:53:44
```

Multicast to all link-local nodes on IPv4:

```console
% sendwol -4 -a 224.0.0.1 -i eth0 00-00-5e-00-53-11
```

More details, refer the man page.

```console
% man ./sendwol.1	# instantly
% man sendwol		# after installation
```


## Installation

1. Download the tarball from [releases page][].

2. Extract them.
	```console
	% tar xvf sendwol-*.**.tar.gz
	```

3. Change the working directory.
	```console
	% cd sendwol-*/.
	```

4. Generate `Makefile`.
	```console
	% ./configure
	```

5. Build the binary.
	```console
	% make
	```

6. Install them.
	```console
	% make install
	```

## Uninstallation

```console
% make uninstall
```


## License

The 2-Clause BSD License

[releases page]: https://github.com/KusaReMKN/sendwol/releases/latest
