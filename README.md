# Argonaut Emails

An email client for the desktop, like Thunderbird, written in C++17 with Qt 6
Widgets: the same technology and the same look as
[Argonaut](https://github.com/jgiannop/argonaut), the file manager.

Version 0.2.0: the tray, and messages that open at once.

## What it does

- **Accounts** over IMAP and SMTP: Gmail, Outlook, Yahoo, iCloud, GMX and any
  provider that offers IMAP. The servers are filled in from the address for
  the big providers. Passwords are kept in the desktop's keyring (GNOME
  Keyring, KWallet), never in a settings file.
- **Three panes**: accounts and folders, the folder's messages (newest first,
  unread in bold, ★ for starred), and the message under them.
- **Reading**: messages look as their sender made them, as in Gmail: they
  are shown by Chromium (Qt WebEngine), locked so that a message can only be
  looked at: no scripts, nothing from the network, and its own pictures only.
  Pictures from the internet (they tell the sender when and where a message
  was read) load only when you click **Show Pictures** on that message.
  Attachments are saved with a click. A link opens in your browser only
  after you've seen where it really goes.
- **Gmail**: the inbox has Gmail's tabs (Primary, Promotions, Social,
  Updates, Forums), with the same messages in each as in Gmail; All Mail,
  Sent Mail, Drafts, Spam and Trash are at the top of the folders.
- **Writing**: new messages, Reply, Reply All and Forward (with the
  attachments), Cc and Bcc, attachments up to 25 MB. A copy goes to Sent.
- **Keeping things in sync**: messages read, starred or deleted here are
  changed on the server, and changes made elsewhere show up here. Opened
  messages are kept on this computer, so they open again without the
  network. It checks for new mail every five minutes, and F5 checks now.
- **Delete** moves a message to the account's Trash.
- **Themes**: Argonaut's themes, including theme files from
  `~/.local/share/argonaut/themes`.
- **In the tray**, as Viber: closing the window leaves the program in the
  system tray, still checking for mail (View → Keep Running in the Tray;
  File → Quit ends it). The icon shows the number of unread messages, as a
  phone's mail does, and so does the dock's icon. A click on it opens a
  popup of your newest messages, ten more each time you scroll to its end;
  a click on one opens it. For Gmail the number and the popup are those of
  Primary.
- **Command palette** (Ctrl+Shift+P), as in Argonaut: type a few letters of
  any command and press Enter.
- **Extensions**, as in Argonaut: small programs that add commands (Edit →
  Extensions, Ctrl+Shift+X). How to write one: [docs/extensions.md](docs/extensions.md).
  The first comes with the program: **Last Week with Claude** (File →
  Summarize Last Week with Claude…): Claude reads the last seven days of
  your inbox and writes a short report of what it was about, grouped by
  subject, with what may need you first. You're told what will be sent
  before anything is, and you choose whether Claude reads only senders and
  subjects or also the beginning of each message. It needs your Anthropic
  API key (Edit → Claude), which stays in your keyring; extensions never see
  it. Every answer is kept, with everything that was sent, as a page in
  `~/.local/share/Argonaut/argonaut-emails/claudeReports` (File → Claude
  Reports).

## Security

- A password is only sent over an encrypted connection (SSL/TLS or
  STARTTLS), and a server whose certificate can't be checked is refused. An
  unencrypted connection is allowed only to this computer.
- Nothing a message contains can add mail headers or SMTP commands: line
  breaks in names and subjects are removed, and addresses are checked.
- The mail kept on this computer (`~/.local/share/Argonaut/argonaut-emails/mail.sqlite`)
  can be read only by you.

## Installing

Each [release](https://github.com/jgiannop/argonaut-emails/releases) has a
`.deb` for Ubuntu 24.04, Ubuntu 26.04 and Debian 13. Download the one for
your system and install it with apt, which also installs what it needs:

```bash
sudo apt install ./argonaut-emails_0.2.0_ubuntu24.04_amd64.deb
```

It appears in the applications as **Argonaut Emails**, with its icon: an
orange letter with Argonaut's boat on it.

## Building

Dependencies (Ubuntu 24.04): `cmake ninja-build qt6-base-dev qt6-webengine-dev libqt6sql6-sqlite`.

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
