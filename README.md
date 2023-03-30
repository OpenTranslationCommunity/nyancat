# Nyancat CLI

在终端中使用Nyancat。

[！[Nyancats]（http://nyancat.dakko.us/nyancat.png）]（http://nyancat.dakko.us/nyancat.png）

## 发行

Nyancat可在以下发行版中使用：

- [Arch]（https://www.archlinux.org/packages/?q=nyancat）
- [Debian]（http://packages.qa.debian.org/n/nyancat.html）
- [Fedora]（https://src.fedoraproject.org/rpms/nyancat）
- [Gentoo]（http://packages.gentoo.org/package/games-misc/nyancat）
- [Mandriva]（http://sophie.zarb.org/rpms/928724d4aea0efdbdeda1c80cb59a7d3）
- [Ubuntu]（https://launchpad.net/ubuntu/+source/nyancat）

还可以在某些BSD系统上使用：

- [FreeBSD]（http://www.freshports.org/net/nyancat/）
- [OpenBSD]（http://openports.se/misc/nyancat）
- [NetBSD]（http://pkgsrc.se/misc/nyancat）

## 安装

首先构建C应用程序：

     make && cd src

您可以独立运行C应用程序。

     ./nyancat

要使用telnet服务器，您需要添加运行以下配置：

     nyancat-t

我们推荐使用`openbsd-inetd`，但`xinetd`和`systemd`也可以正常工作。您
应该也能使用任何其他兼容的`inetd`版本。

## 特定于发行版的信息

#### Debian / Ubuntu

Debian和Ubuntu通过`nyancat`包提供nyancat二进制文件。 A是
`nyancat-server`软件包可自动设置和启用nyancat
安装时的telnet服务器。我不是这些包的维护者;
请将任何问题或错误直接报告给相关发行版的错误跟踪
系统。

## 许可证，参考资料，等。

Nyancat动画的原始来源是
[prguitarman]（http://www.prguitarman.com/index.php?id=348）。

此处提供的代码是根据的条款提供的
[NCSA许可证]（http://en.wikipedia.org/wiki/University_of_Illinois/NCSA_Open_Source_License）。