# Arduino-Serial-Plotter
ImGui driven graph monitor for use in combination with https://github.com/devinaconley/arduino-plotter

<!--![example](https://user-images.githubusercontent.com/25020235/135802654-96345113-1916-4d33-92b9-1e4b1cf7f931.png)-->
<!--![TjwGi7kLoC](https://user-images.githubusercontent.com/25020235/136124350-dfc99226-8b70-46f6-a251-1ff3db09ea32.gif)-->
<!--![gui in demo mode](https://user-images.githubusercontent.com/25020235/136124484-5220ac64-de19-4d21-800d-ee8ad84a8265.gif)-->
![gui in demo mode](https://user-images.githubusercontent.com/25020235/137197483-b983d35d-d65c-4d85-b2d0-83ed4cdb51f4.gif)

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