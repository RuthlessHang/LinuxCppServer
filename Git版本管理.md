# Git 上传 GitHub 完整教程

---

## 一、首次上传（从 0 到 GitHub）

### 第 1 步：配置 git 身份（只需一次）

```bash
git config --global user.name "你的GitHub用户名"
git config --global user.email "你的GitHub邮箱"
```

**作用**：告诉 git 你是谁，每次提交都会记录这个身份。`--global` 表示全局配置，所有项目都用这个身份。

---

### 第 2 步：初始化本地仓库

```bash
cd /home/yaohang/桌面/MakeCppServer
git init
```

**作用**：在当前目录创建 `.git` 隐藏文件夹，把这里变成 git 仓库。

---

### 第 3 步：添加文件到暂存区

```bash
git add .
```

**作用**：把当前目录下所有文件"选中"，准备提交。`.` 表示当前目录。

**补充**：
- `git add 文件名` — 只添加某个文件
- `git add .` — 添加所有文件

---

### 第 4 步：提交到本地仓库

```bash
git commit -m "提交说明"
```

**作用**：创建一个"版本快照"，保存当前所有文件的状态到本地。`-m` 后面是说明这次改了什么。

**例子**：
```bash
git commit -m "添加 Buffer 缓冲区功能"
```

---

### 第 5 步：关联远程仓库

```bash
git remote add origin https://github.com/用户名/仓库名.git
```

**作用**：告诉本地 git 要推送到哪个 GitHub 仓库。`origin` 是远程仓库的别名（约定俗成的名字）。

**例子**：
```bash
git remote add origin https://github.com/RuthlessHang/LinuxCppServer.git
```

---

### 第 6 步：拉取远程仓库内容（首次必做）

```bash
git pull origin master --allow-unrelated-histories --no-rebase
```

**作用**：如果 GitHub 仓库有 README 等文件，需要先拉下来合并。`--allow-unrelated-histories` 允许合并两个独立历史。

**注意**：如果弹出 nano 编辑器，按 `Ctrl+X` → `Y` → 回车 保存退出。

---

### 第 7 步：推送到 GitHub

```bash
git push -u origin master
```

**作用**：把本地提交上传到 GitHub。`-u` 记住关联关系，以后直接 `git push` 就行。

---

## 二、日常更新工作流（记住这三步）

```bash
# 1. 添加改动的文件
git add .

# 2. 提交一个新的版本
git commit -m "这次改了什么"

# 3. 推送到 GitHub
git push
```

**作用**：每次改完代码，执行这三步就完成了一次更新。

---

## 三、常用命令速查

### 查看状态

```bash
git status          # 查看哪些文件改了
git log --oneline   # 查看提交历史（每条一行）
git diff            # 查看具体改了什么
```

### 文件状态颜色

| 颜色 | 含义 |
|------|------|
| 绿色 | 新文件，未跟踪 |
| 黄色 | 已修改，未提交 |
| 灰色 | 被忽略 |

### 撤销操作

```bash
git checkout -- 文件名   # 丢弃未提交的修改
git reset HEAD 文件名    # 从暂存区移除文件
git commit --amend      # 修改上一次提交的说明
```

---

## 四、完整流程图

```
首次上传：
git init → git add . → git commit → git remote add origin → git pull → git push

日常更新：
改代码 → git add . → git commit -m "说明" → git push
```

---

## 五、避坑指南

### Q: push 时提示输入密码？

GitHub 已不支持账号密码，需要用 **Personal Access Token** 代替。

获取方法：GitHub → Settings → Developer settings → Personal access tokens → Generate new token

### Q: push 被拒绝？

```bash
# 远程有你本地没有的提交，先拉取合并
git pull origin master --no-rebase
git push
```

### Q: 想撤销最后一次 commit？

```bash
# 保留文件改动，只撤销 commit
git reset HEAD~1

# 保留文件到暂存区
git reset --soft HEAD~1
```

---

## 六、三层结构图示

```
工作区（你改的文件）
    ↓  git add .
暂存区（选中的文件）
    ↓  git commit -m "说明"
本地仓库（.git文件夹）
    ↓  git push
远程仓库（GitHub）
```

---

# 七、Windows 环境 Git 管理

## 环境准备

### 1. 安装 Git for Windows

访问 [https://git-scm.com/download/win](https://git-scm.com/download/win) 下载安装包

安装时一路默认即可，选择编辑器时建议选 **VS Code** 或 **Nano**

### 2. 打开 Git Bash

安装完成后，在任意文件夹右键 → **Git Bash Here** 打开终端

或在开始菜单搜索 **Git Bash**

---

## Windows 特有配置

### 配置换行符（重要！）

```bash
# 推荐配置：让 git 自动处理 Windows/Linux 换行符差异
git config --global core.autocrlf true
```

**作用**：Windows 用 `\r\n`（CRLF），Linux 用 `\n`（LF），这个配置自动转换，避免冲突。

### 配置默认编辑器

```bash
# 设置为 VS Code（推荐）
git config --global core.editor "code --wait"

# 或设置为 Notepad++
git config --global core.editor "'C:/Program Files/Notepad++/notepad++.exe' -multiInst -notabbar -nosession -noPlugin"
```

### 配置中文显示

```bash
# 让 git status 等命令的中文正常显示
git config --global core.quotepath false
```

---

## Windows 首次上传流程

```bash
# 1. 配置身份
git config --global user.name "你的GitHub用户名"
git config --global user.email "你的GitHub邮箱"

# 2. 进入项目目录
cd /d D:\projects\MakeCppServer

# 3. 初始化仓库
git init

# 4. 添加文件
git add .

# 5. 提交
git commit -m "初始提交"

# 6. 关联远程
git remote add origin https://github.com/用户名/仓库名.git

# 7. 拉取合并（首次）
git pull origin master --allow-unrelated-histories --no-rebase

# 8. 推送
git push -u origin master
```

**注意**：Windows 下路径用 `/d` 切换盘符，如 `cd /d D:\projects`

---

## Windows 日常更新

```bash
# 三步流程（与 Linux 相同）
git add .
git commit -m "更新说明"
git push
```

---

## Windows 常见问题

### Q: 中文乱码？

```bash
git config --global core.quotepath false
git config --global gui.encoding utf-8
```

### Q: push 时提示输入 Token？

GitHub 已不支持账号密码登录，需要生成 Personal Access Token：

1. 访问 [https://github.com/settings/tokens](https://github.com/settings/tokens)
2. 点击 **Generate new token**
3. 选择权限（至少勾选 `repo`）
4. 复制生成的 Token（只显示一次！）
5. push 时输入 Token 作为密码

### Q: 想用 SSH 免密登录？

```bash
# 1. 生成 SSH 密钥
ssh-keygen -t ed25519 -C "你的邮箱"

# 2. 复制公钥
cat ~/.ssh/id_ed25519.pub

# 3. 粘贴到 GitHub → Settings → SSH and GPG keys → New SSH key

# 4. 修改远程仓库地址
git remote set-url origin git@github.com:用户名/仓库名.git
```

### Q: 文件换行符冲突？

```bash
# 查看当前配置
git config core.autocrlf

# 如果是 false，改为 true
git config --global core.autocrlf true

# 重新规范化
git add --renormalize .
git commit -m "规范化换行符"
```

### Q: 想用图形界面？

GitHub Desktop：[https://desktop.github.com/](https://desktop.github.com/)

SourceTree：[https://www.sourcetreeapp.com/](https://www.sourcetreeapp.com/)

---

## 八、Linux vs Windows 命令对比

| 操作 | Linux (Bash) | Windows (Git Bash) |
|------|-------------|-------------------|
| 进入目录 | `cd /home/user/project` | `cd /d D:\project` |
| 初始化 | `git init` | `git init` |
| 添加文件 | `git add .` | `git add .` |
| 提交 | `git commit -m "说明"` | `git commit -m "说明"` |
| 推送 | `git push` | `git push` |
| 换行符配置 | 不需要 | `git config core.autocrlf true` |
| 中文配置 | 不需要 | `git config core.quotepath false` |
