# Arduino-Serial-Plotter
Nuklear driven graph monitor for use in combination with https://github.com/devinaconley/arduino-plotter

![example](https://user-images.githubusercontent.com/25020235/135802654-96345113-1916-4d33-92b9-1e4b1cf7f931.png)

# Compiling
Quick and easy install vcpkg and integrate it with msvc then run (if msvc doesn't pick up on the required libs anyway)
```
vcpkg install glew:x64-windows-static
vcpkg install glfw3:x64-windows-static
```

In your cmake settings make sure to set the same triplet (or add them directly to the CMakeLists.txt)
```
set(VCPKG_TARGET_TRIPLET "x64-windows-static")
```

Worst comes to worst with vcpkg not kicking in, manually set the paths
```
set(GLEW_DIR "<whereever you installed vcpkg>\installed\x64-windows-static\share\glew")
set(glfw3_DIR "<whereever you installed vcpkg>\installed\x64-windows-static\share\glfw")
```