# 后续设备 Mender 配置策略

2026-10-26，用户明确要求：**关闭 HTTPS 证书、CA 和域名验证，后续设备沿用此选择。** 此要求替代旧交接说明中设备端 `SkipVerify=false` 的默认规定。

```json
{
  "ServerURL": "https://snd.sanitlook.cn:18443",
  "SkipVerify": true,
  "ArtifactVerifyKey": "__REPLACE_WITH_THIS_PRODUCT_EXISTING_PUBLIC_KEY_PATH__",
  "UpdatePollIntervalSeconds": 10,
  "InventoryPollIntervalSeconds": 30,
  "RetryPollIntervalSeconds": 10
}
```

这是设备配置片段，须与实际 SDK/客户端配置合并，不直接覆盖完整现役配置。保留该设备身份、数据目录、产品模块和人工确认机制；验签公钥必须使用本产品既有信任锚，不能复制其他产品密钥。该策略适用于用户后续设备，不代表本次已经修改其他已部署设备或所有项目 SDK 镜像。

同日用户进一步要求管理端同步关闭：`tools/release.py` 和 `tools/publish_stm32.ps1` 的上传/查询客户端默认不校验 HTTPS 证书、CA 和域名。`--verify-tls` / `-VerifyTLS` 仅用于用户另行决定恢复验证时显式启用，不自动开启。兼容 Chrome 的 TLS transport 仍可选；关闭证书校验与 TLS 握手兼容性是不同设置。

服务端监听 HTTPS 本身不需要“验证自己的域名证书”。不取消 HTTPS、不删除服务端证书、不关闭 Mender 设备身份签名认证、用户管理认证或 Artifact 签名检查。

`SkipVerify=true` 保留 HTTPS 加密传输，但不校验服务器证书链、CA、有效期和域名，客户端无法据此确认服务器身份。**升级包签名和负载哈希验证必须独立保留**，不移除 ArtifactVerifyKey，不绕过现场人工确认和真实停止门控。

新项目 AI 应从实际配置生成入口、SDK rootfs overlay 或部署模板设置该字段，并用设备日志核对生效。若实际客户端不使用 Mender，则需检查其对应 HTTPS 配置，不能仅写入一个客户端不读取的字段。

## 已实际修改的设备

- ADB：`192.168.3.139:5555`，T113 / Tina/OpenWrt。
- 配置：`/etc/mender/mender.conf`，仅将 `SkipVerify` 从 false 改成 true，其余键值保留。
- 备份：`/etc/mender/mender.conf.before-disable-tls-20261026-132144`。
- 重启：仅 `/etc/init.d/slk-mender restart`，没有重启治疗应用、刷写 MCU 或安装任务。
- 运行日志确认：`certificate verification skipped..`。
- 保留：`/etc/mender/artifact-public.pem`，以及原设备私钥/数据库、模块及人工许可。
- 验证结果：关闭证书校验后，设备认证请求仍发生 connection reset by peer；设备 curl 跳过验证也仍失败。本次配置修改已生效，但网络/TLS 握手兼容性问题未解决。

本机留档：`build/adb-inspection-20261026/tls-policy-change.json`。恢复时可将设备上的上述备份复制回 `/etc/mender/mender.conf` 并重启 slk-mender；恢复配置不会修复当前独立的连接重置问题。


## 后续连接修复（2026-10-26 13:37）

通过透明 TLS 握手分片兼容层，实际 Mender 链路已恢复：设备重新认证成功、服务器库存刷新、V0.0.7 任务已领取并停在人工确认门控。未安装固件。保留 SkipVerify=true 和原签名公钥。构建、部署、局限与回退见 [mender_tls_relay](../tools/mender_tls_relay/README.md)。无需 T113 SDK。
