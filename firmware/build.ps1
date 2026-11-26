# Build the firmware from Windows: runs build.sh inside a WSL distro that has nds32le-elf-gcc.
#   .\build.ps1            -> build/fw.elf, build/fw.bin (flash @0x10000), build/fw.ota.bin
#   .\build.ps1 clean
# The distro defaults to "Arch"; set $env:OIPC_WSL_DISTRO to use another one.
$distro = if ($env:OIPC_WSL_DISTRO) { $env:OIPC_WSL_DISTRO } else { "Arch" }
$here = (Resolve-Path $PSScriptRoot).Path
$wslPath = (wsl.exe -d $distro -e wslpath -a ($here -replace '\\', '/')).Trim()
wsl.exe -d $distro -e bash "$wslPath/build.sh" @args
exit $LASTEXITCODE
