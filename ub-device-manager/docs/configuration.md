# ub-device-manager 配置文件说明

## 1. 配置文件位置

RPM 安装后的配置文件为：

```text
/etc/ub_device_manager/ub_device_manager.toml
```

## 2. 默认配置

配置文件使用 TOML 格式，默认内容如下：

```toml
title = "UB Device Manager"

[log]
level = "INFO"
max_file_size = "10 MB"
max_file_count = 10
stdout = false

[vm]
max_xml_size = "1 MB"
```

## 3. 配置项说明

| 配置项 | 默认值 | 说明                                                                            |
| --- | --- |-------------------------------------------------------------------------------|
| `title` | `"UB Device Manager"` | 配置文件标题                                                                        |
| `log.level` | `"INFO"` | 日志级别，支持 `DEBUG`、`INFO`、`ERROR`                                                |
| `log.max_file_size` | `"10 MB"` | 单个日志文件的轮转阈值，日志文件内容超过该值会进行打包                                                   |
| `log.max_file_count` | `10` | 轮转日志的最大保留数量                                                                   |
| `log.stdout` | `false` | 是否同时输出日志到标准输出。 |
| `vm.max_xml_size` | `"1 MB"` | 创建虚拟机时允许输入的基础 XML 最大字节数，超过限制时拒绝该 XML。                                         |

日志文件固定为 `/var/log/ub_device_manager/ub_device_manager.log`。

## 4. 修改配置并生效

修改配置文件后，等待正在执行的操作结束，再重启服务使修改生效：

```bash
systemctl restart ub-device-manager
systemctl status ub-device-manager --no-pager
```

安装与启动步骤见[安装部署指导](./install.md)。
