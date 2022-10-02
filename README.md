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
% man ./sendwol.1
```

## License

The 2-Clause BSD License

[releases page]: https://github.com/KusaReMKN/sendwol/releases/latest
