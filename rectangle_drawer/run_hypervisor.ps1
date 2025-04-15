pushd "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
./signtool sign /v /f D:\hacking\driver\MyCert.pfx /p 9708 /fd SHA256 "D:\hacking\hypervisor\Hypervisor From Scratch\Build\Debug\Driver\MyHypervisorDriver.sys"
popd

& "C:\Windows\System32\sc.exe" create myHyperDbg type= kernel binPath= "D:\hacking\hypervisor\Hypervisor From Scratch\Build\Debug\Driver\MyHypervisorDriver.sys"
& "C:\Windows\System32\sc.exe" start myHyperDbg