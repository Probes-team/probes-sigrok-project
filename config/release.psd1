@{
    PackageName = 'probes-sigrok'
    # The canonical rolling artifact is always named current. Pass -Version
    # explicitly to scripts/package.ps1 only for an intentional archive/release.
    Version = 'current'
    Target = 'x86_64-w64-mingw32'
    PythonMode = 'embedded'
    SmokeDriver = 'demo'
}
