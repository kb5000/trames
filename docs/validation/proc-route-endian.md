# route -n 的 IPv4 字节序

用户在1.0.0 build28发现 `192.168.2.0/24` 显示成 `0.2.168.192`、
掩码显示成 `0.255.255.255`。原因是 `/proc/net/route` 格式化使用
`u32::from(ip).to_le()`；IP转换本身已经生成网络字节意义的数值，
在little-endian宿主上to_le不会翻转它。Linux该proc接口需要guest原生序的
in_addr十六进制字，BusyBox route按该ABI解释，因此地址/掩码反转。

统一直接网络和bridge/veth行的格式化：IP/网关按octets转little-endian数值，
前缀产生的网络掩码也按相同ABI序列化。回归按BusyBox读取方式解析输出，
检查192.168.2.0/24、默认网关、172.17.0.0/16以及/32主机路由，验证地址、
掩码、网关、flags和metric；Linux该项和VFS严格Clippy通过。Apple在[build29 CI](https://github.com/kb5000/trames/actions/runs/39134234642)
定向检查同一项通过，未重跑未变网络栈全套验收。

修复示例：

```
192.168.2.0     0.0.0.0       255.255.255.0   U  ... eth0
```

[测试日志](mmu-va-pressure/route-test.log)与[Clippy](mmu-va-pressure/route-clippy.log)。
