# Mini-RV32IMA Difftest Integration

这个文档说明如何将 mini-rv32ima 作为 NEMU 的 difftest 参考模型。

## 文件说明

- `difftest.c` - difftest API 包装层，提供与 Spike 兼容的接口
- `Makefile.difftest` - 编译脚本，生成动态链接库
- `README_DIFFTEST.md` - 本文档

## 编译动态链接库

```bash
cd /home/romeo/mini-rv32ima/mini-rv32ima
make -f Makefile.difftest ARCH=riscv32
```

编译成功后会生成：`build/riscv32-mini-rv32ima-so`

## 使用方法

### 方法 1: 直接使用命令行参数

```bash
cd /home/romeo/ysyx-workbench/nemu
./build/riscv32-nemu-interpreter \
    --diff=/home/romeo/mini-rv32ima/mini-rv32ima/build/riscv32-mini-rv32ima-so \
    <your-image.bin>
```

### 方法 2: 通过 NEMU 配置系统

1. 配置 NEMU：
```bash
cd /home/romeo/ysyx-workbench/nemu
make menuconfig
```

2. 在配置菜单中选择：
   - `Testing and Debugging` -> `Enable differential testing` -> `Yes`
   - `Reference design` -> 选择 `Mini-RV32IMA`

3. 保存配置并编译：
```bash
make
```

4. 运行：
```bash
make run
```

## Difftest API 接口

mini-rv32ima 实现了以下标准 difftest API：

- `difftest_init(int port)` - 初始化参考模型
- `difftest_memcpy(uint32_t addr, void *buf, size_t n, bool direction)` - 同步内存
- `difftest_regcpy(void* dut, bool direction)` - 同步寄存器
- `difftest_exec(uint64_t n)` - 执行 n 条指令
- `difftest_raise_intr(uint64_t NO)` - 触发中断
- `difftest_store(uint32_t waddr, uint32_t *wdata)` - 获取写入的数据

## 工作原理

1. **动态链接库加载**：
   - NEMU 使用 `dlopen()` 加载 `riscv32-mini-rv32ima-so`
   - 使用 `dlsym()` 解析 difftest API 函数

2. **状态同步**：
   - 初始化时，NEMU 将内存镜像和寄存器状态复制到 mini-rv32ima
   - 每条指令执行后，NEMU 和 mini-rv32ima 都执行相同的指令
   - 执行后比较两者的寄存器和 CSR 状态

3. **差异检测**：
   - 如果发现不一致，NEMU 会打印详细的差异信息并终止
   - 这有助于快速定位处理器实现中的错误

## 工作流程图

```
NEMU (DUT)                    Mini-RV32IMA (REF)
    │                              │
    ├─ dlopen() ──────────────────>│ 加载 .so
    │                              │
    ├─ difftest_init() ───────────>│ 初始化
    │                              │
    ├─ difftest_memcpy() ─────────>│ 同步内存
    ├─ difftest_regcpy() ─────────>│ 同步寄存器
    │                              │
    │  每条指令执行后：             │
    ├─ 执行指令                     │
    ├─ difftest_exec(1) ──────────>│ 执行相同指令
    ├─ difftest_regcpy() ─────────>│ 获取状态
    ├─ 比较寄存器                   │
    │                              │
    └─ 如果不一致 → 报错并终止      │
```

## 与 Spike 的区别

| 特性 | Spike | Mini-RV32IMA |
|------|-------|--------------|
| 实现语言 | C++ | C |
| 代码复杂度 | 高（完整的 RISC-V 模拟器） | 低（单文件实现） |
| 编译依赖 | 需要 Spike 库 | 无外部依赖 |
| 性能 | 较快 | 较快 |
| 指令集支持 | 完整 RISC-V | RV32IMA |
| 特权级支持 | 完整 | M-mode 和 U-mode |

## 调试技巧

如果 difftest 失败，可以：

1. 查看 NEMU 输出的差异信息，定位不一致的寄存器
2. 使用 `make run` 时添加 `-l` 参数生成指令跟踪日志
3. 对比 NEMU 和 mini-rv32ima 的执行轨迹

## 注意事项

- mini-rv32ima 默认分配 128MB RAM
- 不支持浮点指令（F/D 扩展）
- 不支持压缩指令（C 扩展）
- MMIO 范围：0x10000000 - 0x12000000

## 故障排除

### 编译错误

如果编译时找不到 `mini-rv32ima.h`，确保文件在同一目录下。

### 运行时错误

如果 NEMU 无法加载动态库，检查：
1. 文件路径是否正确
2. 文件是否有执行权限：`chmod +x build/riscv32-mini-rv32ima-so`
3. 使用 `ldd` 检查依赖：`ldd build/riscv32-mini-rv32ima-so`

### Difftest 不一致

如果出现大量不一致：
1. 确认 NEMU 和 mini-rv32ima 使用相同的初始状态
2. 检查是否有未实现的指令
3. 验证 CSR 寄存器的实现是否一致

## 技术细节

### 编译参数说明

- `-fPIC`: Position Independent Code，生成位置无关代码，动态库必需
- `-shared`: 生成共享库（.so 文件）
- `-fvisibility=hidden`: 隐藏所有符号，只导出标记为 `__EXPORT` 的函数
- `-std=gnu99`: 使用 GNU C99 标准

### 符号导出

使用 `__attribute__((visibility("default")))` 导出 difftest API 函数：

```c
#define __EXPORT __attribute__((visibility("default")))

__EXPORT void difftest_init(int port) {
    // ...
}
```

### 验证导出的符号

```bash
nm -D build/riscv32-mini-rv32ima-so | grep difftest
```

应该看到 6 个导出的函数：
- difftest_init
- difftest_memcpy
- difftest_regcpy
- difftest_exec
- difftest_raise_intr
- difftest_store
