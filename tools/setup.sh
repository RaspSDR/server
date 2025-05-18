alpine_url=${alpine_url:-http://mirrors.tuna.tsinghua.edu.cn/alpine/v3.21}

tools_tar=apk-tools-static-2.14.6-r3.apk
tools_url=$alpine_url/main/armv7/$tools_tar

test -f $tools_tar || curl -L $tools_url -o $tools_tar

mkdir -p alpine-apk
tar -zxf $tools_tar --directory=alpine-apk --warning=no-unknown-keyword

root_dir=alpine-root

root_dir=alpine-root

mkdir -p $root_dir/usr/bin
cp /usr/bin/qemu-arm-static $root_dir/usr/bin/

mkdir -p $root_dir/etc
cp /etc/resolv.conf $root_dir/etc/

mkdir -p $root_dir/etc/apk

cp -r alpine-apk/sbin $root_dir/

chroot $root_dir /sbin/apk.static --repository $alpine_url/main --update-cache --allow-untrusted --initdb add alpine-base

echo $alpine_url/main > $root_dir/etc/apk/repositories
echo $alpine_url/community >> $root_dir/etc/apk/repositories

chroot $root_dir /bin/sh <<- EOF_CHROOT

apk update
apk add openssh-server wpa_supplicant git dhcpcd dnsmasq u-boot-tools hostapd iptables avahi dbus chrony gpsd curl-dev htop frp jq libunwind zlib noip2 noip2-openrc netpbm musl-dev linux-headers g++ gcc cmake make minify fftw-dev fdk-aac-dev pkgconf perl gpsd-dev libunwind-dev zlib-dev sqlite-dev sqlite-static libconfig-static libconfig-dev patch automake autoconf

EOF_CHROOT

