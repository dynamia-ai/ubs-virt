# ub-device-manager 单元测试指导

## 1. 环境准备

执行单元测试（UT）前，需要准备：

- Python 3.11 和对应的 pip。
- ub-device-manager 完整源码，包含 `test/` 目录。

### 1.1 进入源码目录

```bash
cd ubs-virt/ub-device-manager
```

后续命令均在该目录执行。

### 1.2 安装测试依赖

```bash
python3.11 -m pip install \
  -i https://pypi.org/simple \
  -r requirements-test.txt
```

该命令同时安装业务代码依赖、pytest 和 pytest-cov，无需单独安装 `requirements.txt`。

## 2. 运行 UT

### 2.1 运行全部测试

```bash
python3.11 -m pytest
```

显示每个用例的执行结果：

```bash
python3.11 -m pytest -v
```

### 2.2 运行指定模块或文件

仅运行 NPU 相关测试：

```bash
python3.11 -m pytest test/domain/npu/
```

仅运行指定测试文件：

```bash
python3.11 -m pytest test/domain/npu/test_npu_tasks.py
```

### 2.3 运行指定用例

```bash
python3.11 -m pytest -v \
  test/domain/npu/test_npu_tasks.py::TestQueryNpuTask::test_keeps_only_npu_devices
```

## 3. 查看测试结果

执行结束后，终端会显示通过、失败等结果，以及代码覆盖率。

默认生成以下报告：

| 报告 | 路径 |
| --- | --- |
| UT 执行报告 | `report/ut-report.xml` |
| 覆盖率 XML 报告 | `report/coverage.xml` |
| 覆盖率 HTML 报告 | `report/htmlcov/index.html` |

使用浏览器打开 `report/htmlcov/index.html`，可以查看各文件的覆盖率及未覆盖的代码行。

每次执行都会更新上述报告。需要完整测试报告时，应运行全部测试。

## 4. 新增 UT 用例

### 4.1 选择测试位置

优先在对应模块已有的测试文件中补充用例。

| 测试内容 | 目录或文件 |
| --- | --- |
| 公共配置、日志和任务框架 | `test/common/` |
| NPU 管理 | `test/domain/npu/` |
| 虚拟机管理和 XML 处理 | `test/domain/vm/` |
| 应用层接口和数据模型 | `test/app/` |
| Python SDK | `test/ub_dm_sdk/` |
| CLI | `test/test_ub_device_manager_cli.py` |

新增测试文件和用例时，遵循以下命名规则：

- 文件名：`test_*.py`。
- 测试函数或方法名：`test_*`。
- 使用测试类时，类名：`Test*`。

### 4.2 编写用例

参考同目录已有测试，按照以下步骤编写：

1. 准备输入数据，并模拟外部依赖。
2. 调用被测试函数或方法。
3. 使用 `assert` 检查结果，或使用 `pytest.raises` 检查预期异常。

测试应覆盖正常、边界和异常场景。修复问题时，应补充能够复现该问题的回归用例。

涉及外部依赖时，需要注意：

- libvirt、UBSE、命令执行和 HTTP 请求等调用应使用 Mock，避免操作真实资源。
- 异步方法使用 `AsyncMock` 模拟。
- 文件操作使用 pytest 的 `tmp_path` 临时目录，避免修改正式配置。

项目已在 `test/conftest.py` 中支持异步测试，可以直接编写 `async def test_*`，无需额外安装 pytest-asyncio。

### 4.3 验证新增用例

先运行修改的测试文件：

```bash
python3.11 -m pytest -v test/domain/npu/test_npu_tasks.py
```

将路径替换为实际修改的测试文件。

用例通过后，运行全部测试，确认未影响其他功能：

```bash
python3.11 -m pytest
```

## 5. 常见问题

| 问题 | 处理方法 |
| --- | --- |
| 提示缺少 Python 模块 | 使用 Python 3.11 重新安装 `requirements-test.txt`。 |
| 提示无法识别 `--cov` 参数 | 检查是否已安装 pytest-cov。 |
| 无法导入 `ub_device_manager` | 确认在组件根目录执行 `python3.11 -m pytest`。 |
| 新增测试未被发现 | 检查测试文件、类和函数的命名是否符合规则。 |
| 提示 libvirt 或 UBSE Stub 接口未模拟 | 在用例中 Mock 对应调用，并设置返回值或异常。 |