## 构建流程

创建 lib 目录

``` shell
mkdir lib
```

找个其他目录 clone detours

``` shell
git clone https://github.com/microsoft/Detours.git
```

打开 x86 Native Tools Command Prompt for VS 2022

定位到 detours 目录

``` shell
cd /d D:/xxx/Detours
```

构建 detours

``` shell
nmake
```

将 `Detours/lib.X86/detours.lib` 复制到 `lib` 目录

下载 libass msvc 构建 <https://github.com/ShiftMediaProject/libass/releases>

解压 `lib/x86/ass.lib` 到 `lib` 目录

构建 `filter.dll` 和 `loader.exe`

``` shell
mkdir build
cd build
cmake -A Win32 ..
cmake --build . --config Release
```

在 `build/Release` 文件夹下找到 `filter.dll` 和 `loader.exe`

将下载的 libass msvc 构建中的 `bin/x86/ass.dll` 放到 `loader.exe` 同目录下
