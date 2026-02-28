## 构建流程

### 安装 vcpkg

找个其他目录 clone vcpkg

``` shell
git clone https://github.com/microsoft/vcpkg.git
```

在 vcpkg 目录下执行

``` shell
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg install zlib:x86-windows-static
.\vcpkg\vcpkg install libpng:x86-windows-static
```

### 构建 fptool

在 fptool 目录下执行

``` shell
mkdir build
cd build
cmake -A Win32 -DCMAKE_TOOLCHAIN_FILE="[vcpkg路径]/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static ..
cmake --build . --config Release
```

在 `build/Release` 文件夹下能找到 `fptool.exe` 和 `rgba2rgb.exe`
