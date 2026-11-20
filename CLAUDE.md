# CLAUDE.md

Guidance for Claude Code in this repository.

## Project

Argonaut Emails: an email client (IMAP + SMTP) in C++17 and Qt 6 Widgets, a sibling of Argonaut, the file manager (`~/dev/argonaut`), with the same look and the same rules. Minimum Qt 6.4 (Ubuntu 24.04). License GPL-3.0-or-later.

## Build and test

```bash
cmake --preset debug && cmake --build build/debug
ctest --preset debug                       # all tests
./build/debug/tests/tst_sync foldersAndMessages
```

The debug build uses `-Wall -Wextra -Wshadow=local` and **is free of warnings: keep it so.** Every new source goes into `CMakeLists.txt`.

Tests are Qt Test, one line each in `tests/CMakeLists.txt` (`mail_test(name [LIBRARY mail_app])`). **Every test's `main()` is `MAIL_TEST_MAIN`** (`tests/TestSupport.h`): a private HOME and XDG folders, offscreen, `MAIL_KEYSTORE=file`, so tests never touch the real settings, mail store or keyring. **Tests never talk to a real mail server**: `tests/FakeServers.h` has `FakeImap` and `FakeSmtp` on 127.0.0.1, which log what they were asked. `MAIL_TEST_GRAB=<file.png> tst_mainwindow` saves a picture of the window.

## Architecture

- `mail_core` (`src/core`, no widgets, all of it tested):
  - `ImapParser`: the IMAP wire format. `takeResponse` cuts one response off the buffer, literals included (`{n}\r\n` + n bytes, limit 64 MB); `parseResponse` gives the tag, the status, the `[code]` and the values (atoms keep their brackets: `BODY[HEADER.FIELDS (FROM)]`). Mailbox names are modified UTF-7 (`decodeMailboxName`/`encodeMailboxName`). `quoted()` refuses CR/LF/NUL, so nothing can smuggle a second command in.
  - `ImapClient`: one connection on `QSslSocket`, commands one at a time (tags `A1`…), each gets its untagged responses. Login: CAPABILITY, STARTTLS when asked, then `AUTHENTICATE PLAIN` (SASL-IR or a continuation) or `LOGIN`. **A password only over TLS; `Security::None` only to 127.0.0.1/::1/localhost** (the tests); certificate errors are never ignored. `commandWithLiteral` for APPEND. The destructor and `close()` disconnect the socket first: a socket closing while we are destroyed must say nothing.
  - `SmtpClient`: one message per connection (EHLO, STARTTLS, AUTH PLAIN or LOGIN, MAIL FROM, RCPT TO, DATA with `dotStuffed`). Same TLS rule. Addresses with spaces, `<`, `>` or control characters are refused before anything is sent.
  - `Mime`: reading messages: headers (unfolded; `header()` decodes encoded words, `rawHeader()` doesn't), multipart trees (depth 20, 1000 parts at most), base64/quoted-printable, charsets through `QStringDecoder` (Qt with ICU: windows-1253, iso-8859-7… all work), RFC 2231 parameters, addresses (split on the raw header first: an encoded name may hold a comma), dates. **An attachment's name is only its last part** (`../../.bashrc` → `.bashrc`). `Attachment::part` points into the `Message`: keep the message alive.
  - `Composer`: writing messages. UTF-8 quoted-printable text, base64 attachments under multipart/mixed, encoded words of at most 66 characters, every line ≤ 78. **Line breaks are removed from everything the user typed into a header** (`oneLine`), and an address that isn't plain is dropped: no header injection (`tst_composer noHeadersSmuggledIn`). Bcc is only in `recipients()`, never in the message.
  - `MailStore`: SQLite (`QSQLITE`) in `$XDG_DATA_HOME/argonaut-emails/mail.sqlite` (0600, folder 0700): `folders` and `messages` (summary columns + `body` once opened). **A new UIDVALIDITY drops what was kept of the folder.** Signals `folderChanged`, `foldersChanged`.
  - `Sync`: one account; a queue of operations (each selects its folder first), one IMAP connection, opened when needed. A folder: SELECT, then the newest `FirstFetch` (500) summaries the first time, `UID FETCH <highest+1>:*` after (the server answers `n:*` with the last message even when nothing is newer: filtered), then the flags of what is kept (`removeMissing` for what went). Bodies are fetched once and kept. Flags are changed in the store at once and on the server after. Delete = `UID MOVE` to the Trash folder, else COPY + `\Deleted` + `UID EXPUNGE` (only with UIDPLUS; **a plain EXPUNGE would remove every message marked deleted**). `keepSent` APPENDs to Sent (not on Gmail, `X-GM-EXT-1`, which keeps its own). Polls every 5 minutes. A folder's role (inbox, all, sent, drafts, trash, junk, archive, starred) comes from its SPECIAL-USE flags; **names are guessed only when the server flags no folder at all** (on Gmail a label "Deleted Items" isn't the Trash). All Mail (`\All`) is shown right after the Inbox.
  - `Account` (settings `accounts/<id>/…`, `accounts/order`), `Passwords` (Argonaut's `KeyStore` made per account: Secret Service over QtDBus with `application=argonaut-emails`, `account`, `type=imap|smtp`; a 0600 file without a keyring; `MAIL_KEYSTORE=file` in tests).
  - `SafeHtml::clean`: what Qt's rich-text view shows of a message's HTML: scripts, styles, head, frames, objects, forms, comments and `on…` attributes out; `href` only http(s)/mailto/#; pictures only `cid:` (remote ones counted, their alt text kept).
- `mail_app` (`src/app`): `MainWindow` (three panes: `FolderTree` | `MessageList` over `MessageView`), `ComposeWindow`, `AccountDialog`, `StyleSheet`, and `Theme.{h,cpp}` **copied from Argonaut** (themes, theme files in `argonaut/themes`, `glyphIcon`). `MailBrowser::loadResource` returns only the message's own `cid:` pictures: nothing from the network or the disk. A clicked link asks first.

## Rules

- Never ignore TLS errors, never send a password without TLS.
- Everything in a message is data from a stranger: no path from an attachment's name, nothing loaded from the network when showing a message.
- Never call EXPUNGE without UIDPLUS (`UID EXPUNGE`).

## Working with the owner

- **Ask before every commit**; push and release only when asked. Commits use the address already in this repository's git config (a GitHub noreply address); never put the owner's personal email anywhere.
- **Never add Claude attribution or `Co-Authored-By` lines** to commits or pull requests.
- No remote yet: the GitHub repository `jgiannop/argonaut-emails` is to be made by the owner, then added as `origin` and pushed.
- Never type a password or an app password for the owner, and never connect a test to a real mail account: the owner adds accounts in the program themselves.
- Prefer plain-text questions and direct action; keep the debug build free of warnings and every test passing.
