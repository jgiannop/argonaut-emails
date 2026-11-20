# Argonaut Emails

An email client for the desktop, like Thunderbird, written in C++17 with Qt 6
Widgets: the same technology and the same look as
[Argonaut](https://github.com/jgiannop/argonaut), the file manager.

Version 0.1.0: the first working version.

## What it does

- **Accounts** over IMAP and SMTP: Gmail, Outlook, Yahoo, iCloud, GMX and any
  provider that offers IMAP. The servers are filled in from the address for
  the big providers. Passwords are kept in the desktop's keyring (GNOME
  Keyring, KWallet), never in a settings file.
- **Three panes**: accounts and folders, the folder's messages (newest first,
  unread in bold, ★ for starred), and the message under them.
- **Reading**: HTML messages are shown without scripts, styles from the
  internet or pictures from the internet (those tell the sender when and
  where a message was read; the window says how many were left out).
  Pictures sent inside the message are shown. Attachments are saved with a
  click. A link opens only after you've seen where it really goes.
- **Writing**: new messages, Reply, Reply All and Forward (with the
  attachments), Cc and Bcc, attachments up to 25 MB. A copy goes to Sent.
- **Keeping things in sync**: messages read, starred or deleted here are
  changed on the server, and changes made elsewhere show up here. Opened
  messages are kept on this computer, so they open again without the
  network. It checks for new mail every five minutes, and F5 checks now.
- **Delete** moves a message to the account's Trash.
- **Themes**: Argonaut's themes, including theme files from
  `~/.local/share/argonaut/themes`.

## Security

- A password is only sent over an encrypted connection (SSL/TLS or
  STARTTLS), and a server whose certificate can't be checked is refused. An
  unencrypted connection is allowed only to this computer.
- Nothing a message contains can add mail headers or SMTP commands: line
  breaks in names and subjects are removed, and addresses are checked.
- The mail kept on this computer (`~/.local/share/argonaut-emails/mail.sqlite`)
  can be read only by you.

## Building

Dependencies (Ubuntu 24.04): `cmake ninja-build qt6-base-dev libqt6sql6-sqlite`.

```bash
cmake --preset debug
cmake --build build/debug
./build/debug/argonaut-emails
ctest --preset debug
```

## Not there yet

OAuth sign-in (Gmail and Outlook without an app password), IDLE (mail
arriving at once instead of every five minutes), drafts, threads, searching
the server, moving between folders, HTML writing, address book, and the
terminal version.

## License

GPL-3.0-or-later.
