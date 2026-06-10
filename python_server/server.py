try:
    from python_server.clawmachine.claw_machine import ClawMachine
except ModuleNotFoundError:
    from clawmachine.claw_machine import ClawMachine


def run():
    ClawMachine()


if __name__ == "__main__":
    run()
