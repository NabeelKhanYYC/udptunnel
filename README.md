UDP tunnel over TCP. License: LGPLv2.

Please build it using the following commands:

```bash
rm -rf cmake-build
cmake -S . -B cmake-build
cmake --build cmake-build
cmake --build cmake-build --target package
```

After that you will receive packages for DEB-based and RPM-based distros:

```
cmake-build/udptunnel-*.amd64.deb
cmake-build/udptunnel-*.amd64.rpm
```
