#!/system/bin/sh
# Keeps the remote way in open while the game eats the memory (root; Magisk / KernelSU service.d or by hand):
#  - Tailscale, Termux (the ssh session) and adbd get oom_score_adj -1000 every second (the activity manager raises
#    it again on every state change), so the low-memory killer passes them over;
#  - Tailscale that is gone (crashed, killed, swiped away and force-stopped) or has no tunnel is started again with
#    its CONNECT_VPN broadcast (to a stopped package too: FLAG_INCLUDE_STOPPED_PACKAGES; checked every second while the
#    process is gone); after 60 s without a tunnel its activity is opened as a last resort (again every 60 s). A tunnel of another
#    VPN app is left alone; Tailscale is kept exempt from battery optimisation (see exempt);
#    While it is down the device is kept awake (wake lock "ts-guard"): a force-stop drops Tailscale's own wake-ups and a
#    sleeping device runs nothing of this until something else wakes it (the Pad, 2026-10-22: 9 minutes);
#  - adb over TCP (5555) is turned on again when its property was lost.
# Install: cp ts-guard.sh /data/adb/service.d/ && chmod 755; start now: su -c 'setsid sh ts-guard.sh &'
# Log: /data/local/tmp/ts-guard.log
PKG=com.tailscale.ipn
LOG=/data/local/tmp/ts-guard.log
PIDF=/data/local/tmp/ts-guard.pid
[ -f $PIDF ] && kill -0 "$(cat $PIDF)" 2>/dev/null && grep -q ts-guard /proc/"$(cat $PIDF)"/cmdline 2>/dev/null && exit 0
echo $$ > $PIDF
echo -1000 > /proc/$$/oom_score_adj
log() { echo "$(date '+%m-%d %H:%M:%S') $*" >> $LOG; }
until [ "$(getprop sys.boot_completed)" = "1" ]; do sleep 2; done
log "start (pid $$)"
uid_of() { stat -c %u /data/data/$1 2>/dev/null; }
# Android lets an app start its foreground service from the background (the CONNECT_VPN broadcast below starts the
# VPN service) only when it is exempt from battery optimisation; without it a Tailscale killed under memory pressure
# came back without its tunnel until its activity was opened (the Marble, 2026-10-22: 60 s down). Done once per start.
exempt() {
  dumpsys deviceidle whitelist +$PKG >/dev/null 2>&1
  cmd appops set $PKG RUN_ANY_IN_BACKGROUND allow >/dev/null 2>&1
  cmd appops set $PKG RUN_IN_BACKGROUND allow >/dev/null 2>&1
  am set-standby-bucket $PKG active >/dev/null 2>&1
  am set-inactive $PKG false >/dev/null 2>&1
}
exempt
tunnel_up() { ip -o -4 addr 2>/dev/null | grep -q 'tun[0-9]* *inet 100\.'; }
other_vpn() { ip -o -4 addr 2>/dev/null | grep 'tun[0-9]* *inet ' | grep -vq 'inet 100\.'; }
down=0; n=0; since=0; opened=0
while true; do
  U1=$(uid_of $PKG); U2=$(uid_of com.termux)
  for p in $(ps -A -o PID,UID,NAME | awk -v a="$U1" -v b="$U2" '($2==a && a!="") || ($2==b && b!="") || $3=="adbd" || $3=="sshd" {print $1}'); do
    [ "$(cat /proc/$p/oom_score_adj 2>/dev/null)" = "-1000" ] || echo -1000 > /proc/$p/oom_score_adj 2>/dev/null
  done
  # every second when the process is gone (lmkd kills it while the game loads), else every 5 s
  if [ -n "$U1" ] && { [ $((n % 5)) = 0 ] || ! pidof $PKG >/dev/null; }; then
    if pidof $PKG >/dev/null && tunnel_up; then
      [ $down -gt 0 ] && { log "tailscale back after $(( $(date +%s) - since )) s ($down tries)"; echo ts-guard > /sys/power/wake_unlock 2>/dev/null; }
      down=0
    elif other_vpn; then
      down=0                                   # another VPN holds the tunnel: not ours to fight
    else
      down=$((down + 1))
      [ $down = 1 ] && { since=$(date +%s); opened=0; echo ts-guard > /sys/power/wake_lock 2>/dev/null; }
      log "tailscale $(pidof $PKG >/dev/null && echo 'has no tunnel' || echo 'not running'): connect ($down)"
      am broadcast -f 0x20 -a com.tailscale.ipn.CONNECT_VPN -n $PKG/.IPNReceiver >/dev/null 2>&1
      [ $((down % 20)) = 1 ] && exempt
      if [ $(( $(date +%s) - since )) -ge $(( 60 * (opened + 1) )) ]; then
        opened=$((opened + 1))
        log "still down: opening the app"
        monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
      fi
    fi
    if [ "$(getprop service.adb.tcp.port)" != "5555" ] && [ "$(getprop persist.adb.tcp.port)" != "5555" ]; then
      log "adb tcp port lost: 5555 again"
      setprop service.adb.tcp.port 5555; stop adbd; start adbd
    fi
  fi
  n=$((n + 1))
  sleep 1
done
