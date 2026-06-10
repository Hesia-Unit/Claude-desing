# Jetson-Ajax Docker Simulation

This Docker harness is a host-side simulation scaffold. It does not emulate the Jetson kernel, device tree, NVIDIA Jetson GPU drivers, I2C/SPI hardware, or real flight hardware.

It does:

- mount the Jetson-Ajax rootfs read-only;
- expose the HESIA repository read-only;
- expose the Jetson-Ajax workspace read/write for logs;
- verify Jetson rootfs identity;
- probe aarch64 userspace through `qemu-aarch64-static` when possible.

Required environment variables when running from PowerShell:

```powershell
$env:JETSON_ROOTFS_HOST='\\wsl.localhost\Kali\mnt\f\Jetson-Ajax\mounts\rootfs'
$env:HESIA_REPO_HOST='C:\Users\matis\Documents\Hesia-Firmware'
$env:JETSON_WORKSPACE_HOST='F:\Jetson-Ajax'
docker compose -f F:\Jetson-Ajax\docker\compose.yaml run --rm jetson-sim
```

