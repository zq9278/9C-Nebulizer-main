# Mender TLS 握手分片转发

用于部分网络路径中 ClientHello 整包发送被重置的兼容处理。不需要 T113 SDK；独立静态 ARMv7 程序只转发 TLS 字节，不解密、不替换证书、不改变 Mender 协议。首个 TLS record 的第一个字节单独发送，等待 50ms 后继续转发。限制回环监听、固定上游、最多 16 个连接。

## 构建与测试

Linux/WSL，Go 1.22 或更新：

```sh
cd tools/mender_tls_relay
gofmt -w main.go main_test.go
go test -race ./...
CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 go build -trimpath -ldflags=-s -o ../../build/mender-tls-relay-armv7 .
```

测试覆盖 CONNECT 目标限制、透明模式、双向大数据内容一致性及上游证书透传。它们是本机测试，不代表实机安装升级。

## T113 部署

本次设备 `192.168.3.139:5555`，Tina/OpenWrt procd。安装前备份 `/etc/hosts`、`/etc/init.d/slk-mender`、`/etc/mender/mender.conf`；不要覆盖设备身份、状态库、签名公钥或人工确认脚本。

1. 将 ARM 程序安装到 `/usr/bin/slk-mender-tls-relay`，将本目录 `slk-mender-tls-relay.init` 安装到 `/etc/init.d/slk-mender-tls-relay`，权限均为 755，脚本必须 LF 换行。
2. 保留 hosts 原有内容，添加一行 `127.0.0.1 snd.sanitlook.cn`。服务上游使用 `47.111.69.184:18443`，避免 hosts 导致递归连接。公网 IP 变化时须更新上游并重新验证；不要将该 IP 无条件套用到其他服务器。
3. Mender 保持原来的直接 TLS 连接，不添加 `HTTP_PROXY` / `HTTPS_PROXY`。本设备旧客户端的 CONNECT 路径未继承 SkipVerify，不能用于本次部署。
4. 保留 `ServerURL=https://snd.sanitlook.cn:18443`、用户要求的 `SkipVerify=true` 和现有 `ArtifactVerifyKey`。
5. 启用并启动服务，然后重启 Mender：

```sh
/etc/init.d/slk-mender-tls-relay enable
/etc/init.d/slk-mender-tls-relay start
/etc/init.d/slk-mender restart
```

START=78，早于 Mender 的 START=80；STOP=12，晚于 Mender 的 STOP=10。仅监听 127.0.0.1，不开放局域网代理。此 hosts 规则也影响本机其他访问该域名的进程，但仅 18443 端口部署了转发。

验证须以真实客户端认证、服务器库存刷新和任务门控为准。不能把单独 curl 成功视为升级成功，也不能为了测试跳过人工确认。

## 2026-10-26 实机记录

- 原始 Mender 请求及直接 curl 在新入口出现 connection reset；关闭证书验证不能解决。分片握手对照可成功，具体重置发生在哪一层尚未确定。
- 已安装静态 ARMv7 程序，SHA256：`4b3fd0739d5c9af91df4a5d2fe7e314fce086d956156f1fb548e2dacfe13257d`。
- 13:37:50（北京时间）：原客户端 `Reauthorization successful`；13:37:51 库存提交成功，服务器库存时间 `2026-10-26T05:37:51.039Z`。
- V0.0.7.20261026 任务 `c07d0b5d-ab1a-4b07-ae1b-74c500c49ada` 已领取，服务器显示 downloading，子状态 `Download_Enter_00_slk-consent`。日志明确等待现场确认和新鲜安全 MCU 遥测。这是下载前等待，尚未下载/验签/安装，未测试 MCU 升级成功。
- 保留原客户端、公钥、设备身份、治疗程序及下载/安装确认脚本。未重启板卡、未烧写 MCU。直接设备 curl 仍出现 wolfSSL 握手错误，不能声称所有 TLS 客户端均修复；实际 Mender 认证与任务链路已恢复。
- 已配置开机服务链接；本次没有整机重启验证。

## 回退本次兼容层

本次原文件备份：`/overlay/slk-nebulizer/mender/tls-relay-backup-20261026-1334/`。以下恢复到分片兼容层安装前，保留 SkipVerify=true；原网络重置可能再次出现。

```sh
/etc/init.d/slk-mender stop
/etc/init.d/slk-mender-tls-relay stop
/etc/init.d/slk-mender-tls-relay disable
cp /overlay/slk-nebulizer/mender/tls-relay-backup-20261026-1334/hosts /etc/hosts
cp /overlay/slk-nebulizer/mender/tls-relay-backup-20261026-1334/slk-mender.init /etc/init.d/slk-mender
chmod 755 /etc/init.d/slk-mender
/etc/init.d/slk-mender start
```

不要恢复/删除 Mender 状态库，也不要在任务已进入实际安装时执行服务回退。
