"""scripts/west_extensions.py

Sample west extension command for the zephyr-esp32-example project.

Run it with:
    west project-hello --board <board> <message>

See https://docs.zephyrproject.org/latest/develop/west/extensions.html
"""

from textwrap import dedent

from west.commands import WestCommand


class ProjectHello(WestCommand):
    def __init__(self):
        super().__init__(
            "project-hello",  # gets stored as self.name
            "",  # ignored self.help, will not be required by future west versions
            description=dedent(
                """
            Sample Project west extension command.
            """
            ),
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, description=self.description)

        levels = ["dbg", "inf", "wrn", "err"]
        parser.add_argument(
            "-l",
            "--level",
            default="inf",
            choices=levels,
            help="log level for the message (default: %(default)s)",
        )
        parser.add_argument("message", help="message to print")

        return parser  # gets stored as self.parser

    def do_run(self, args, unknown):
        log_fn = {
            "dbg": self.dbg,
            "inf": self.inf,
            "wrn": self.wrn,
            "err": self.err,
        }[args.level]

        self.inf(f"[{self.name}] log level: {args.level}")
        log_fn(f"[{self.name}] message: {args.message}")
