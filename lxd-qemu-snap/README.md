# External QEMU snap for LXD snap

## How to build and use

```
cd lxd-qemu-snap

# clean up previous builds
rm -f qemu-for-lxd_*.snap

# build
snapcraft

# install
sudo snap install qemu-for-lxd_*.snap --devmode

# connect snaps
sudo snap connect lxd:gpu-2404 mesa-2404:gpu-2404
sudo snap connect lxd:qemu-external qemu-for-lxd:qemu-external
```

## How to use with virgl (only specific to this example of snapcraft.yaml)

```
lxc init images:ubuntu/noble/desktop desktop -c limits.memory=8GiB --vm

# modify instance configuration:
lxc config edit desktop

# choose an appropriate renderer:
ls -la /dev/dri/by-path/*-render

# for example, on my system it is /dev/dri/by-path/pci-0000:67:00.0-render
# lspci | grep -E "(3D|VGA)" shows:
# 67:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Rembrandt (rev d8)

# add the following lines:
  raw.apparmor: |-
    /snap/lxd/*/gpu-2404/** mr,
    /dev/dri/ r,
    /dev/dri/card[0-9]* rw,
    /dev/dri/renderD[0-9]* rw,
    /run/udev/data/c226:[0-9]* r,  # 226 drm
    /sys/devices/** r,
    /sys/bus/** r,
  raw.qemu: -display egl-headless,rendernode=/dev/dri/by-path/pci-0000:67:00.0-render
  raw.qemu.conf: |-
    [device "qemu_gpu"]
    driver = "virtio-vga-gl"

# try it
lxc start desktop --console=vga

# you can check output from:
dmesg | grep -i drm
# if you see:
# [drm] features: +virgl ...
# it's a good sign

glxinfo | grep -i vir
# it should show something like:
# Device: virgl ...
```

## References:

https://github.com/snapcore/snapd/blob/5c8d8431baa425464b279ff26b8c44eecb9aab22/interfaces/builtin/opengl.go#L41

https://gitlab.gnome.org/GNOME/gnome-boxes/-/issues/586