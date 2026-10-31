# Build

Needs an LLVM 18 toolchain with an arm64-apple-ios linker (lld). No iOS SDK required.

```
clang -target arm64-apple-ios15.0 -dynamiclib -O2 -fno-stack-protector \
      -fuse-ld=lld -nostdlib -undefined dynamic_lookup \
      -Wl,-adhoc_codesign -Wl,-platform_version,ios,15.0,15.0 \
      Tweak.c -o CC15.dylib
```

Then drop CC15.dylib + CC15.plist into a rootless deb under
`/var/jb/Library/MobileSubstrate/DynamicLibraries/`.
