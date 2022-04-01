Sysprof is a sampling profiler that uses a kernel module to generate
stacktraces which are then interpreted by the userspace program
"sysprof".

See the [Sysprof homepage](http://sysprof.com/) for more information.

Merge requests and bug reports should be sent to sysprof's [repository on
GNOME's GitLab instance](https://gitlab.gnome.org/GNOME/sysprof). For general
discussion and questions, you can create a new topic in [GNOME's
Discourse](https://discourse.gnome.org).

The former mailing list is archived in
<https://mail.gnome.org/archives/sysprof-list/>.

Debugging symbols
-----------------

The programs and libraries you want to profile should be compiled
with `-fno-omit-frame-pointer` and have debugging symbols available,
or you won't get much usable information.


Building Sysprof
----------------

You need some packages installed. The package names may vary depending on your
distribution, the following command works on Fedora 36:

    sudo dnf install gcc gcc-c++ ninja-build gtk4-devel libadwaita-devel

Then do the following:

    meson --prefix=/usr build
    cd build
    ninja
    sudo ninja install

**WARNING**: `ninja install` will mostly install under the configured install
prefix but installs systemd service configuration directly in the system
default location `/usr/lib/systemd` so it won't work without root privileges,
even if the install prefix is a user-owned directory.
