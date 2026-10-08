# UI
- ChatPanel:聊天窗口，收发消息，用户与ai的交流，ai的工具调用等都现在在里面
- Setting.hpp:设置api,base_url等（默认字段为deepseek的）
- AgentArea.hpp: 包含了ChatPanel与SettingPanel,为AI窗口的整体
- Show.panel: 独立于AgentArea,常驻在菜单栏中。点击切换AgentPanel的可见性。不可见时不占据空间。可见时占据全局的右侧，可拉伸
# 管理
- Context.hpp:上下文管理
- Communicate.hpp: 收发信息
- Controller.hpp: 调度层，解析信息后进行行动，用户发送消息时。收到ai的回复后进行解析，决定应该是函数调用，还是对话，还是做其他的
# 工具
所有工具写在./Skills/下，且都在list.xml中注明函数描述，参数类型，何时应该使用等。
xml格式定义：
```
(请填充)
```