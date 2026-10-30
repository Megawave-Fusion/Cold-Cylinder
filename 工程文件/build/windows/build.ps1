[CmdletBinding()]
param(
    [string]$CompilerRoot,
    [string]$TclTkRoot,
    [string]$OutputDirectory,
    [switch]$NoDownload,
    [switch]$CrossCompile
)

# Windows PowerShell 5.1 also reads this UTF-8 script correctly because it has a BOM.
# All generated C strings use UTF-8 bytes in octal form, independent of the host ACP.
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$NativeWindows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$WorkbenchRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $WorkbenchRoot '..'))
$CompilerVersion = '20261020'
$CompilerArchiveName = 'llvm-mingw-20261020-ucrt-x86_64.zip'
$CompilerUrl = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip'
$CompilerSha256 = 'fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd'
# The bundled runtime is checksum-locked and cached by its content digest.
$RuntimeSha256 = 'd418e1d0d127469d39dc6c2d694dadce466dde16be8579f5c2f094e31c018460'
$StageRoot = $null
$OldPath = $env:PATH

function Find-Tool([string]$Directory, [string[]]$Names) {
    foreach ($name in $Names) {
        $path = Join-Path $Directory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return [IO.Path]::GetFullPath($path)
        }
    }
    return $null
}

function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ('Tool failed ({0}): {1}' -f $LASTEXITCODE, $Tool)
    }
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Expected -and $actual -ne $Expected.ToLowerInvariant()) {
        throw ('SHA-256 mismatch for {0}. Expected {1}; got {2}.' -f $Path, $Expected, $actual)
    }
    return $actual
}

function Expand-CachedArchive([string]$Archive, [string]$Destination, [string]$Hash) {
    $marker = Join-Path $Destination '.cold-cylinder-complete'
    if ((Test-Path -LiteralPath $marker -PathType Leaf) -and
        ([IO.File]::ReadAllText($marker).Trim() -eq $Hash)) {
        return
    }
    if (Test-Path -LiteralPath $Destination) {
        throw ('Incomplete dependency cache: {0}. Remove it or use an explicit dependency path.' -f $Destination)
    }
    $temporary = $Destination + '.extract-' + [Guid]::NewGuid().ToString('N')
    try {
        New-Item -ItemType Directory -Path $temporary -Force | Out-Null
        Expand-Archive -LiteralPath $Archive -DestinationPath $temporary
        [IO.File]::WriteAllText((Join-Path $temporary '.cold-cylinder-complete'), $Hash)
        Move-Item -LiteralPath $temporary -Destination $Destination
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Recurse -Force
        }
    }
}

function Get-Compiler([string]$Root) {
    if (Test-Path -LiteralPath $Root -PathType Leaf) {
        return [IO.Path]::GetFullPath($Root)
    }
    $names = @('x86_64-w64-mingw32-clang.exe', 'x86_64-w64-mingw32-clang',
               'clang.exe', 'clang', 'gcc.exe', 'gcc')
    foreach ($directory in @((Join-Path $Root 'bin'), $Root)) {
        $found = Find-Tool $directory $names
        if ($found) { return $found }
    }
    return $null
}

function Test-Compiler([string]$Compiler) {
    if (-not $Compiler) { return $false }
    $triple = (& $Compiler '-dumpmachine' 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $triple -notmatch '^x86_64-.*(mingw|windows-gnu)') {
        return $false
    }
    return $true
}

function Resolve-Compiler {
    if ($CompilerRoot) {
        $found = Get-Compiler $CompilerRoot
        if (-not (Test-Compiler $found)) {
            throw 'CompilerRoot must contain an x86_64 MinGW-w64 UCRT Clang/GCC toolchain. MSVC/clang-cl are unsupported because the solver uses C11 complex.'
        }
        return $found
    }
    $roots = @($env:COLD_CYLINDER_COMPILER_ROOT, 'C:/llvm-mingw', 'C:/msys64/ucrt64')
    if ($env:MSYS2_ROOT) { $roots += (Join-Path $env:MSYS2_ROOT 'ucrt64') }
    foreach ($root in $roots) {
        if ($root) {
            $found = Get-Compiler $root
            if (Test-Compiler $found) { return $found }
        }
    }
    foreach ($name in @('x86_64-w64-mingw32-clang.exe', 'clang.exe', 'gcc.exe')) {
        $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue
        if ($command -and (Test-Compiler $command.Source)) { return $command.Source }
    }
    if ($NoDownload) {
        throw 'No x64 MinGW-w64 compiler found. Set -CompilerRoot to llvm-mingw UCRT or MSYS2 ucrt64; MSVC is not supported. Omit -NoDownload to fetch the pinned llvm-mingw package.'
    }
    $archive = Join-Path $CacheRoot $CompilerArchiveName
    if (Test-Path -LiteralPath $archive -PathType Leaf) {
        $null = Assert-Sha256 $archive $CompilerSha256
    } else {
        $download = $archive + '.download-' + [Guid]::NewGuid().ToString('N')
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            Write-Host ('Downloading pinned llvm-mingw {0} UCRT...' -f $CompilerVersion)
            Invoke-WebRequest -Uri $CompilerUrl -OutFile $download -UseBasicParsing
            $null = Assert-Sha256 $download $CompilerSha256
            Move-Item -LiteralPath $download -Destination $archive
        } finally {
            if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Force }
        }
    }
    $destination = Join-Path $CacheRoot ('llvm-mingw-' + $CompilerVersion)
    Expand-CachedArchive $archive $destination $CompilerSha256
    $found = Get-Compiler (Join-Path $destination ([IO.Path]::GetFileNameWithoutExtension($CompilerArchiveName)))
    if (-not (Test-Compiler $found)) { throw 'The verified llvm-mingw archive did not contain the expected x64 compiler.' }
    return $found
}

function Resolve-Runtime {
    if ($TclTkRoot) { return [IO.Path]::GetFullPath($TclTkRoot) }
    $archive = Join-Path $WorkbenchRoot 'deps/windows-x64.zip'
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw 'Missing deps/windows-x64.zip. Restore the bundled Tcl/Tk 8.6.18 Windows runtime or pass -TclTkRoot with bin/include/lib/licenses.'
    }
    $hash = Assert-Sha256 $archive $RuntimeSha256
    $destination = Join-Path $CacheRoot ('runtime/8.6.18-x64-' + $hash.Substring(0, 12))
    Expand-CachedArchive $archive $destination $hash
    if (Test-Path -LiteralPath (Join-Path $destination 'include/tcl.h') -PathType Leaf) { return $destination }
    foreach ($child in @(Get-ChildItem -LiteralPath $destination -Directory)) {
        if (Test-Path -LiteralPath (Join-Path $child.FullName 'include/tcl.h') -PathType Leaf) { return $child.FullName }
    }
    throw 'The Windows runtime archive has no include/tcl.h. Expected bin/include/lib/licenses at its root.'
}

function Find-ImportLibrary([string]$Runtime, [string]$Name) {
    $pattern = '^(lib)?' + $Name + '(86|8\.6)t?(\.dll)?\.(a|lib)$'
    $libraries = @(Get-ChildItem -LiteralPath (Join-Path $Runtime 'lib') -File |
        Where-Object { $_.Name -match $pattern } | Sort-Object Name)
    if ($libraries.Count -ne 1) {
        throw ('Expected one MinGW import library for {0} 8.6 in {1}/lib; stub/static-only libraries are insufficient.' -f $Name, $Runtime)
    }
    return $libraries[0].FullName
}

function Assert-PeX64([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw ('Not a Windows PE file: ' + $Path) }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw ('Expected a Windows x64 PE executable or DLL: ' + $Path)
        }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Write-TclHeader([string]$Destination) {
    $writer = [IO.StreamWriter]::new($Destination, $false, [Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine('/* Generated from UTF-8 Tcl sources by build/windows/build.ps1. */')
        $writer.WriteLine('static const char *const gui_script[] = {')
        foreach ($name in @('rf_gui_views.tcl', 'rf_gui.tcl')) {
            $bytes = [IO.File]::ReadAllBytes((Join-Path $WorkbenchRoot ('ui/' + $name)))
            $start = 0
            for ($index = 0; $index -le $bytes.Length; ++$index) {
                if ($index -lt $bytes.Length -and $bytes[$index] -ne 10) { continue }
                if ($index -eq $bytes.Length -and $start -eq $index) { break }
                $end = $index
                if ($end -gt $start -and $bytes[$end - 1] -eq 13) { --$end }
                $line = New-Object Text.StringBuilder
                $null = $line.Append('"')
                for ($position = $start; $position -lt $end; ++$position) {
                    $null = $line.Append('\' + [Convert]::ToString($bytes[$position], 8).PadLeft(3, '0'))
                }
                $null = $line.Append('\012",')
                $writer.WriteLine($line.ToString())
                $start = $index + 1
            }
        }
        $writer.WriteLine('NULL };')
    } finally { $writer.Dispose() }
}

function Copy-RuntimeDlls([string]$Runtime, [string]$Package) {
    $dlls = @(Get-ChildItem -LiteralPath (Join-Path $Runtime 'bin') -File -Filter '*.dll')
    if (-not @($dlls | Where-Object { $_.Name -match '^tcl86t?\.dll$' }).Count -or
        -not @($dlls | Where-Object { $_.Name -match '^tk86t?\.dll$' }).Count) {
        throw 'The runtime bin directory must contain Tcl/Tk 8.6 x64 DLLs.'
    }
    foreach ($dll in $dlls) {
        Assert-PeX64 $dll.FullName
        Copy-Item -LiteralPath $dll.FullName -Destination $Package
    }
}

function Complete-DllDependencies([string]$Package, [string]$Inspector, [string[]]$SearchDirectories) {
    # API-set names are Windows loader contracts, not redistributable DLL files.
    $systemNames = @('advapi32.dll','bcrypt.dll','comctl32.dll','comdlg32.dll','crypt32.dll',
        'dnsapi.dll','dwmapi.dll','gdi32.dll','imm32.dll','kernel32.dll','msimg32.dll',
        'msvcrt.dll','ncrypt.dll','netapi32.dll','normaliz.dll','ntdll.dll','ole32.dll',
        'oleaut32.dll','rpcrt4.dll','secur32.dll','setupapi.dll','shell32.dll','shlwapi.dll',
        'ucrtbase.dll','user32.dll','userenv.dll','usp10.dll','uxtheme.dll','version.dll',
        'winhttp.dll','wininet.dll','winmm.dll','winspool.drv','wldap32.dll','ws2_32.dll')
    $seen = @{}
    $queue = New-Object 'Collections.Generic.Queue[string]'
    foreach ($file in @(Get-ChildItem -LiteralPath $Package -File -Recurse |
        Where-Object { $_.Extension -in @('.exe', '.dll') })) { $queue.Enqueue($file.FullName) }
    while ($queue.Count) {
        $file = $queue.Dequeue()
        $key = [IO.Path]::GetFileName($file).ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        Assert-PeX64 $file
        $imports = & $Inspector '-p' $file 2>&1
        if ($LASTEXITCODE -ne 0) { throw ('Could not inspect PE imports: ' + $file) }
        foreach ($line in $imports) {
            if ([string]$line -notmatch '^\s*DLL Name:\s*(\S+)\s*$') { continue }
            $dll = $Matches[1]
            # llvm-objdump also prints the exporting DLL's own name; that is
            # already present at $file, including helpers below lib/reg/dde.
            if ($dll -ieq $key) { continue }
            if ($dll -match '^(api|ext)-ms-win-.*\.dll$' -or $dll.ToLowerInvariant() -in $systemNames) { continue }
            $destination = Join-Path $Package $dll
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                $source = $null
                $nested = @(Get-ChildItem -LiteralPath $Package -File -Recurse |
                    Where-Object { $_.Name -ieq $dll })
                if ($nested.Count) { $source = $nested[0].FullName }
                foreach ($directory in $SearchDirectories) {
                    if ($source) { break }
                    # Windows DLL names are case insensitive, including during a macOS cross build.
                    $match = @(Get-ChildItem -LiteralPath $directory -File -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -ieq $dll })
                    if ($match.Count) { $source = $match[0].FullName; break }
                }
                if (-not $source) { throw ('Missing dependency {0}, imported by {1}. Supply it in TclTkRoot/bin or the compiler bin directory.' -f $dll, $key) }
                Assert-PeX64 $source
                Copy-Item -LiteralPath $source -Destination $destination
            }
            $queue.Enqueue($destination)
        }
    }
}

function Assert-PackageDestination([string]$Destination) {
    if (-not (Test-Path -LiteralPath $Destination)) { return }
    if (-not (Test-Path -LiteralPath $Destination -PathType Container)) { throw 'OutputDirectory must be a directory.' }
    if (-not @(Get-ChildItem -LiteralPath $Destination -Force).Count) { return }
    $marker = Join-Path $Destination 'BUILD-WINDOWS.json'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw ('Refusing to replace a nonempty directory without Cold-Cylinder build metadata: ' + $Destination)
    }
    try {
        $metadata = [IO.File]::ReadAllText($marker) | ConvertFrom-Json
        if ($metadata.product -cne 'Cold-Cylinder') { throw 'Unexpected product marker.' }
    } catch { throw ('Invalid Cold-Cylinder build metadata in ' + $Destination) }
}

function Publish-Package([string]$Package, [string]$Zip, [string]$Destination, [string]$ZipDestination) {
    # Recheck immediately before the transaction, as well as before compilation.
    Assert-PackageDestination $Destination
    $suffix = '.previous-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $backup = $Destination + $suffix
    $zipBackup = $ZipDestination + $suffix + '.zip'
    $hadDirectory = Test-Path -LiteralPath $Destination
    $hadZip = Test-Path -LiteralPath $ZipDestination
    $movedDirectory = $false
    $movedZip = $false
    $publishedDirectory = $false
    try {
        if ($hadDirectory) { Move-Item -LiteralPath $Destination -Destination $backup; $movedDirectory = $true }
        if ($hadZip) { Move-Item -LiteralPath $ZipDestination -Destination $zipBackup; $movedZip = $true }
        Move-Item -LiteralPath $Package -Destination $Destination
        $publishedDirectory = $true
        Move-Item -LiteralPath $Zip -Destination $ZipDestination
    } catch {
        if ($publishedDirectory -and (Test-Path -LiteralPath $Destination)) { Remove-Item -LiteralPath $Destination -Recurse -Force }
        if ($movedDirectory) { Move-Item -LiteralPath $backup -Destination $Destination }
        if ($movedZip) { Move-Item -LiteralPath $zipBackup -Destination $ZipDestination }
        throw
    }
    if ($movedDirectory) { Write-Host ('Previous build retained: ' + $backup) }
    if ($movedZip) { Write-Host ('Previous ZIP retained: ' + $zipBackup) }
}

try {
    if (-not $NativeWindows -and -not $CrossCompile) { throw 'Run this script on Windows, or use -CrossCompile with explicit -CompilerRoot and -TclTkRoot.' }
    if ($CrossCompile -and (-not $CompilerRoot -or -not $TclTkRoot)) { throw '-CrossCompile requires explicit -CompilerRoot and -TclTkRoot; Windows toolchains are never downloaded in this mode.' }
    if ($NativeWindows -and -not [Environment]::Is64BitOperatingSystem) { throw 'Cold-Cylinder requires Windows x64.' }
    if ($NativeWindows) {
        $CacheRoot = Join-Path $env:LOCALAPPDATA 'Cold-Cylinder'
        New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
    } else { $CacheRoot = $null }
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $WorkspaceRoot '交付/Windows-x64' }
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $OutputDirectory -PathType Leaf) { throw 'OutputDirectory points to a file.' }
    $outputPrefix = $OutputDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $workspacePrefix = $WorkspaceRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ($workspacePrefix.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory cannot replace the workspace or one of its parents.'
    }
    Assert-PackageDestination $OutputDirectory
    $outputParent = Split-Path -Parent $OutputDirectory
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
    $zipDestination = Join-Path $outputParent 'Cold-Cylinder-Windows-x64.zip'
    $StageRoot = Join-Path $outputParent ('.cold-cylinder-build-' + [Guid]::NewGuid().ToString('N'))
    $build = Join-Path $StageRoot 'build'
    $package = Join-Path $StageRoot 'package'
    New-Item -ItemType Directory -Path $build, $package -Force | Out-Null
    $compiler = Resolve-Compiler
    $compilerBin = Split-Path -Parent $compiler
    $resolvedCompilerRoot = Split-Path -Parent $compilerBin
    $env:PATH = $compilerBin + [IO.Path]::PathSeparator + $OldPath
    $runtime = Resolve-Runtime
    foreach ($relative in @('include/tcl.h','include/tk.h','lib/tcl8.6/init.tcl','lib/tk8.6/tk.tcl','BUILD.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $relative) -PathType Leaf)) { throw ('Missing Tcl/Tk runtime file: ' + $relative) }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runtime 'licenses') -PathType Container)) { throw 'TclTkRoot/licenses is required for redistribution.' }
    $tclLibrary = Find-ImportLibrary $runtime 'tcl'
    $tkLibrary = Find-ImportLibrary $runtime 'tk'
    $resourceCompiler = Find-Tool $compilerBin @('x86_64-w64-mingw32-windres.exe','x86_64-w64-mingw32-windres','llvm-windres.exe','llvm-windres','windres.exe','windres')
    $inspector = Find-Tool $compilerBin @('llvm-objdump.exe','llvm-objdump','x86_64-w64-mingw32-objdump.exe','x86_64-w64-mingw32-objdump','objdump.exe','objdump')
    if (-not $resourceCompiler -or -not $inspector) { throw 'The compiler toolchain must include windres/llvm-windres and llvm-objdump/objdump.' }
    $versionLines = @(& $compiler '--version')
    if ($LASTEXITCODE -ne 0) { throw 'Unable to query the compiler version.' }
    $compilerDescription = $versionLines[0]
    Write-Host ('Compiler: ' + $compilerDescription)
    Write-Host ('Tcl/Tk: ' + $runtime)
    # Reject MSVCRT/32-bit builds early, and verify C11 complex support with real headers.
    $probe = Join-Path $build 'compiler_probe.c'
    [IO.File]::WriteAllText($probe, "#include <_mingw.h>`n#include <complex.h>`n#if !defined(_UCRT) || !defined(_WIN64)`n#error Cold-Cylinder requires x64 MinGW-w64 UCRT`n#endif`ndouble complex rf_probe(double complex z) { return cexp(z) + I; }`n")
    Invoke-Tool $compiler @('-std=c11','-c',$probe,'-o',(Join-Path $build 'compiler_probe.o'))
    Write-TclHeader (Join-Path $build 'rf_gui_script.h')
    $flags = @('-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-finput-charset=UTF-8','-fexec-charset=UTF-8','-DRF_HAVE_TK','-DRF_BUNDLED_TK',
        ('-I' + $build),('-I' + (Join-Path $WorkbenchRoot 'src')),('-I' + (Join-Path $WorkbenchRoot 'ui')),('-I' + (Join-Path $runtime 'include')))
    $objects = @()
    foreach ($source in @(Get-ChildItem -LiteralPath (Join-Path $WorkbenchRoot 'src'),(Join-Path $WorkbenchRoot 'ui') -File -Filter '*.c' | Sort-Object Name)) {
        $object = Join-Path $build ($source.BaseName + '.o')
        Write-Host ('Compiling ' + $source.Name)
        Invoke-Tool $compiler ($flags + @('-c',$source.FullName,'-o',$object))
        $objects += $object
    }
    $resource = Join-Path $build 'Windows.o'
    Push-Location $PSScriptRoot
    try { Invoke-Tool $resourceCompiler @('-i','Windows.rc','-o',$resource,'-O','coff') }
    finally { Pop-Location }
    $objects += $resource
    $libraries = @($tkLibrary,$tclLibrary,'-lm','-lshell32','-luser32','-static-libgcc',
        '-Wl,--nxcompat,--dynamicbase,--high-entropy-va')
    Invoke-Tool $compiler ($objects + @('-mwindows','-Wl,-e,mainCRTStartup') + $libraries + @('-o',(Join-Path $package 'Cold-Cylinder.exe')))
    Invoke-Tool $compiler ($objects + @('-mconsole') + $libraries + @('-o',(Join-Path $package 'Cold-Cylinder-cli.exe')))
    Assert-PeX64 (Join-Path $package 'Cold-Cylinder.exe')
    Assert-PeX64 (Join-Path $package 'Cold-Cylinder-cli.exe')
    Copy-RuntimeDlls $runtime $package
    New-Item -ItemType Directory -Path (Join-Path $package 'lib') -Force | Out-Null
    # tcl8 contains core msgcat modules; reg/dde are Windows Tcl helper packages.
    # Copy script/extension directories only, excluding development import libraries.
    foreach ($name in @('tcl8.6','tcl8','tk8.6','reg1.3','dde1.4')) {
        $source = Join-Path $runtime ('lib/' + $name)
        if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw ('Missing Tcl/Tk runtime directory: lib/' + $name) }
        Copy-Item -LiteralPath $source -Destination (Join-Path $package 'lib') -Recurse
    }
    Complete-DllDependencies $package $inspector @((Join-Path $runtime 'bin'),$compilerBin,
        (Join-Path $resolvedCompilerRoot 'x86_64-w64-mingw32/bin'),
        (Join-Path $resolvedCompilerRoot 'x86_64-w64-mingw32/lib'))
    Copy-Item -LiteralPath (Join-Path $runtime 'licenses') -Destination $package -Recurse
    Copy-Item -LiteralPath (Join-Path $runtime 'BUILD.txt') -Destination (Join-Path $package 'licenses/TclTk-BUILD.txt')
    $compilerNotices = Join-Path $package 'licenses/compiler'
    New-Item -ItemType Directory -Path $compilerNotices -Force | Out-Null
    foreach ($notice in @(Get-ChildItem -LiteralPath $resolvedCompilerRoot -File |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT)' })) { Copy-Item -LiteralPath $notice.FullName -Destination $compilerNotices }
    foreach ($relative in @('licenses','share/licenses')) {
        $notices = Join-Path $resolvedCompilerRoot $relative
        if (Test-Path -LiteralPath $notices -PathType Container) { Copy-Item -LiteralPath $notices -Destination $compilerNotices -Recurse }
    }
    New-Item -ItemType Directory -Path (Join-Path $package 'examples') -Force | Out-Null
    foreach ($example in @(Get-ChildItem -LiteralPath (Join-Path $WorkbenchRoot 'examples') -File |
        Where-Object { $_.Extension -in @('.rfproj','.plasmaconfig') })) { Copy-Item -LiteralPath $example.FullName -Destination (Join-Path $package 'examples') }
    Copy-Item -LiteralPath (Join-Path $WorkspaceRoot 'Start-Cold-Cylinder-Windows.cmd') -Destination $package
    Copy-Item -LiteralPath (Join-Path $WorkspaceRoot 'README.md') -Destination $package
    $manualDirectory = Join-Path $package '天线电流谱文档'
    New-Item -ItemType Directory -Path $manualDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $WorkspaceRoot '天线电流谱文档/Finite_width_antenna_current_spectra.pdf') -Destination $manualDirectory
    $metadata = [ordered]@{
        product = 'Cold-Cylinder'; architecture = 'x86_64'; crt = 'UCRT';
        compiler = [string]$compilerDescription; pinnedCompilerVersion = $CompilerVersion;
        pinnedCompilerArchiveSha256 = $CompilerSha256; bundledRuntimeSha256 = $RuntimeSha256;
        crossCompiled = [bool]$CrossCompile; builtUtc = [DateTime]::UtcNow.ToString('o');
        guiSha256 = (Get-FileHash -LiteralPath (Join-Path $package 'Cold-Cylinder.exe') -Algorithm SHA256).Hash;
        cliSha256 = (Get-FileHash -LiteralPath (Join-Path $package 'Cold-Cylinder-cli.exe') -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText((Join-Path $package 'BUILD-WINDOWS.json'), ($metadata | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    if ($NativeWindows -and -not $CrossCompile) {
        Write-Host 'Checking the packaged CLI...'
        Invoke-Tool (Join-Path $package 'Cold-Cylinder-cli.exe') @('--check')
    } else { Write-Host 'Cross compilation: Windows execution was not checked by this script.' }
    $archive = Join-Path $StageRoot 'Cold-Cylinder-Windows-x64.zip'
    Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -CompressionLevel Optimal
    Publish-Package $package $archive $OutputDirectory $zipDestination
    Write-Host ('Windows package: ' + $OutputDirectory)
    Write-Host ('Windows ZIP: ' + $zipDestination)
    Write-Host 'Start Cold-Cylinder.exe for the GUI; use Cold-Cylinder-cli.exe for terminal commands.'
} catch {
    Write-Error ('Windows build failed: ' + $_.Exception.Message) -ErrorAction Continue
    exit 1
} finally {
    $env:PATH = $OldPath
    if ($StageRoot -and (Test-Path -LiteralPath $StageRoot)) { Remove-Item -LiteralPath $StageRoot -Recurse -Force }
}
