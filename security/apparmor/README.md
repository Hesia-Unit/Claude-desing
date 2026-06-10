# HESIA AppArmor Profiles

This directory provides AppArmor confinement profiles for the deployed HESIA services:

- `hesia-drone`
- `hesia-server`

They complement:

- `systemd` sandboxing
- `seccomp-bpf`
- `NoNewPrivileges=yes`

## Install

```bash
sudo install -d -m 0755 /etc/apparmor.d
sudo install -m 0644 security/apparmor/hesia-drone /etc/apparmor.d/hesia-drone
sudo install -m 0644 security/apparmor/hesia-server /etc/apparmor.d/hesia-server
sudo apparmor_parser -r /etc/apparmor.d/hesia-drone
sudo apparmor_parser -r /etc/apparmor.d/hesia-server
```

## Validate

```bash
sudo aa-status | grep -E 'hesia-(drone|server)'
sudo journalctl -k | grep DENIED
```

## First deployment on a new target

If a new BSP exposes slightly different device nodes, start in complain mode first:

```bash
sudo aa-complain /etc/apparmor.d/hesia-drone
sudo aa-complain /etc/apparmor.d/hesia-server
```
