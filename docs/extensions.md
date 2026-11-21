# Writing an Argonaut Emails extension

An extension is a small program that adds commands to Argonaut Emails. It
can be written in any language. Argonaut Emails starts it and talks to it in
JSON, as [Argonaut](https://github.com/jgiannop/argonaut) does with its own
extensions: same files, same conversation, the same rules.

Version 1 of the protocol lets an extension:

- add **commands** to a menu of the menu bar (and so to the command palette,
  Ctrl+Shift+P) or to the right-click menu of a message; ask a question
  first, or ask for a text;
- say a **line** when something is done, and show a longer **result**
  (Markdown) in the message pane;
- show a **form** for the user to fill in or decide while it works;
- **read the user's messages** through the program, and **ask Claude**
  through the program, with the user's key, only while it does what the user
  chose.

One comes with the program and is good to read: Last Week with Claude
([`extensions/week`](../extensions/week)): it reads the last seven days of
the inbox and has Claude write a short report of what they are about.

## Safety, for you and for the people who use your extension

An extension is an ordinary program with the user's rights. It is not
sandboxed. So:

- An extension from the user's own folder **never runs until the user
  switches it on** in the Extensions window (Edit → Extensions, Ctrl+Shift+X).
- Everything an extension sends is data: texts are cut to size, unknown
  values are dropped. A crash or hang never stops the program; an extension
  that keeps crashing is left off.
- Mail is the user's most private data. An extension reads it only if its
  `extension.json` says so (the Extensions window tells the user), only
  through the program, and only while it does something the user chose.
  Never send a message's contents anywhere without telling the user first
  what will be sent, and to whom.

## Where an extension lives

A folder named after the extension, holding `extension.json` and the program:

```
~/.local/share/Argonaut/argonaut-emails/extensions/
    word-count/
        extension.json
        word-count.py
```

Extensions that come with the program are in
`/usr/share/argonaut-emails/extensions` (or next to the program, in a build
folder). To try a folder of extensions without installing them, start the
program with `MAIL_EXTENSIONS_PATH=/some/folder` (several separated by `:`);
only those are looked at then.

### extension.json

```json
{
    "id": "word-count",
    "name": "Word Count",
    "description": "Counts the words of a message.",
    "version": "1.0",
    "author": "You",
    "protocol": 1,
    "exec": ["word-count.py"],
    "start": "when-needed",
    "commands": [{"id": "count", "label": "Count Words", "multiple": false}]
}
```

| Key | Meaning |
|---|---|
| `id` | Required. Letters, digits, `.`, `-`, `_`; at most 64 characters. If two extensions share an id, the user's copy wins. |
| `protocol` | Required. `1`. |
| `exec` | Required. The program and its arguments, as a list (or one string). A relative program is inside the extension's folder. It must be executable. |
| `name`, `description`, `version`, `author` | Shown in the Extensions window. |
| `start` | `"when-needed"`: your program is started only when one of your commands is chosen, and stopped after about a minute with nothing to do. `commands` then says what to show meanwhile. |
| `uses` | `["claude"]`: you ask Claude through the program ([Asking Claude](#asking-claude)). `["mail"]`: you read the user's messages ([Reading mail](#reading-mail)). Both are shown in the Extensions window. |

## The conversation

The program is started with the extension's folder as the working directory
and `ARGONAUT_EMAILS_EXTENSION_ID` set (and `ANTHROPIC_API_KEY` taken out).
Messages are JSON objects, **one per line**, UTF-8: the program's arrive on
standard input, yours go to standard output. Flush after every line.
Standard error is thrown away. When standard input ends, exit.

Ignore message types and keys you don't know; newer versions may add some.

### From the program

```json
{"type": "hello", "protocol": 1, "app": "argonaut-emails", "version": "0.2.0"}
{"type": "invoke", "command": "count", "selection": [{"account": "maria@example.com", "folder": "Inbox", "role": "inbox",
  "uid": 4521, "from": "Eleni", "fromEmail": "eleni@example.com", "subject": "Dinner", "date": "2026-11-19T18:02:00Z"}], "input": ""}
{"type": "form", "id": "start", "cancelled": false, "values": {"read": "…"}, "button": 0}
{"type": "mail", "id": "m1", "since": "…", "messages": […]}
{"type": "claude", "id": "q1", …}
```

- `invoke`: the user chose one of your commands (and said yes to its
  question). `selection` holds the chosen message for a command on messages,
  and is empty for one in the menu bar. `input` is what was typed, for a
  command with `ask`.
- `form`, `mail`, `claude`: answers to what you asked (below).

### To the program

```json
{"type": "commands", "commands": [{"id": "count", "label": "Count Words"}]}
{"type": "message", "text": "312 words"}
{"type": "show", "title": "Word count", "kind": "markdown", "text": "# 312 words\n…", "note": "One line under the title"}
```

### Commands

| Key | Meaning |
|---|---|
| `id` | Required. Comes back in `invoke`. |
| `label` | Required. Up to 60 characters. |
| `menu` | `"file"`, `"edit"`, `"view"` or `"message"`: at the end of that menu of the menu bar, and in the command palette, where it is called `ext::<label>`. Without it the command is in the right-click menu of a message. |
| `multiple` | `false`: only for exactly one message. |
| `confirm` | A question asked first, with Cancel as the default. Without a yes you get no `invoke`. |
| `ask` | `{"label": "Note:", "multiline": true, "text": "starting text"}`: a text field first; what was typed comes as `input`. |
| `group` | The submenu it is in (`"Tools"`, or `"Tools/More"`). |
| `checked` | `true`: shown ticked. |

At most 20 commands. `message` is shown for a few seconds in the status bar
(up to 200 characters). `show` puts a result in the message pane: `kind` is
`"markdown"` or `"text"`; it is made safe and shown locked as a message is
(no scripts, nothing from the network).

### Forms

While you do what the user chose, you can ask them something:

```json
{"type": "form", "id": "start", "title": "Word Count", "heading": "Count the words",
 "text": "Plain text, line breaks kept.", "warning": "",
 "fields": [{"id": "what", "kind": "choice", "label": "Count", "choices": ["Words", "Letters"], "value": "Words"},
            {"id": "note", "label": "A note", "placeholder": "optional"},
            {"id": "all", "kind": "check", "label": "Every message", "value": false}],
 "buttons": ["Count"]}
```

`kind` is `"text"` (the default), `"secret"`, `"check"` or `"choice"`. The
dialog says which extension asks. The answer comes as a `form` message, with
`"cancelled": true` if the user backed out. One form at a time; answering
one counts as the user choosing again.

### Reading mail

With `"uses": ["mail"]`, while you do what the user chose:

```json
{"type": "mail", "id": "m1", "ask": "messages", "folder": "inbox", "days": 7, "limit": 300, "text": true, "chars": 1500}
```

The messages of that kind of folder (`inbox`, `all`, `starred`, `sent`,
`drafts`, `archive`, `junk`, `trash`) of every account, of the last `days`
(at most 31), newest first, at most `limit` (500):

```json
{"type": "mail", "id": "m1", "since": "2026-11-13T12:00:00", "messages": [
  {"account": "maria@example.com", "folder": "Inbox", "uid": 4521, "from": "Eleni", "fromEmail": "eleni@example.com",
   "to": "maria@example.com", "subject": "Dinner on Sunday", "date": "2026-11-19T18:02:00Z",
   "unread": true, "starred": false, "attachments": false, "tabs": ["primary"], "text": "Shall we meet at eight?"}]}
```

`text` (only when asked) is the beginning of the message's own words, at
most `chars` characters: its plain text, or its HTML made plain, without
quoted replies. Never attachments, never pictures. Texts not kept yet are
fetched from the server (and kept), so asking for them takes longer; a
`warning` says if some couldn't be. `tabs` are Gmail's inbox tabs.

### Asking Claude

With `"uses": ["claude"]`, you have Claude asked something **through the
program**, with the API key and the model the user set in Edit → Claude, at
the user's cost. Your extension never has the key.

```json
{"type": "claude", "id": "q1",
 "request": {"system": "You summarize mail.", "max_tokens": 4000,
             "messages": [{"role": "user", "content": "…"}]}}
```

The whole response comes back, with its text put together and what it cost:

```json
{"type": "claude", "id": "q1", "response": {…}, "text": "…", "stop_reason": "end_turn", "dollars": 0.031,
 "report_file": "/home/you/.local/share/Argonaut/argonaut-emails/claudeReports/word_count_report_20.11.26_12.54.31.md"}
{"type": "claude", "id": "q1", "error": {"message": "There is no Claude API key yet: Edit → Claude", "status": 0}}
```

**Every answer is kept**, as in Argonaut: a Markdown page in the user's
`claudeReports` folder (File → Claude Reports), named after your extension
and the time, with the answer and everything that was sent. You don't do
anything for this and can't switch it off.

Two questions are free: `"ask": "info"` gives `hasKey`, `model` and
`modelName`; `"ask": "count"` (with a `request`) gives `tokens` and `dollars`.
Tell the user before you spend.

Not yours to decide: the `model`, `max_tokens` above 16000, streaming. A
request is sent only while you do something the user chose (a command
within the last five minutes, renewed by each form they answer), at most 12
for one such choice.
