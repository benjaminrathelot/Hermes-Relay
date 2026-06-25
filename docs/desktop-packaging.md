# Desktop Packaging

## Objective

The desktop package should be usable by operators who do not have Python installed manually.

That means the package must ship:

- the Python runtime
- the Python wrapper
- the local UI assets
- the Hermes native shared library
- the Hermes CLI binary used for relay service supervision

## Current Package Inputs

The repository now provides:

- `libhermesrelay` as a shared library
- `hermes-cli`
- `python/` package sources for the wrapper and local shell

## Recommended Packaging Path

For the desktop wrapper, the practical path is:

1. build native artifacts
2. create a Python virtual environment for packaging
3. install the wrapper package
4. bundle the app with PyInstaller
5. include the native library and CLI binary as bundled resources

This direction matches PyInstaller's documented purpose of bundling a Python program and its dependencies into a distributable application or executable.

## Initial Packaging Modes

Preferred first production mode:

- one-folder package

Why:

- easier to inspect
- easier to troubleshoot native library loading
- easier to patch resource layouts during early deployment

Later option:

- one-file package

Why later:

- startup is more opaque
- native resource extraction details become more important
- diagnosis is harder during first field deployments

## Platform Notes

- macOS: package as an app bundle after the basic one-folder layout is stable
- Windows: package as a directory or installer once the Win32 native library validation is complete
- Linux: package as a tarball or native package after the one-folder bundle is validated

## Local Security Rules

Regardless of packaging style:

- the UI API must remain loopback-only by default
- the native library must be loaded from the packaged application path, not from arbitrary system locations
- the relay transport listener remains separately configured from the UI listener

## Current State

The repository now contains the wrapper foundation and package layout, but not yet a final PyInstaller build script tied to each OS release pipeline. That should be added only after the UI contract and resource layout settle.
