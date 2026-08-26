'''
Script to setup logging with colors and verbosity levels.
'''

import logging

def setup_logger(logger, level: str):
    """
    Configure logging with colors and a chosen verbosity level
    """
    logger.handlers.clear()  # avoid duplicated logs if reconfigured

    class CustomFormatter(logging.Formatter):
        info_debug_color = "\x1b[38;5;14m"
        yellow = "\x1b[33;20m"
        red = "\x1b[31;20m"
        bold_red = "\x1b[31;1m"
        reset = "\x1b[0m"
        format = "%(asctime)s - %(name)s - %(levelname)s - %(message)s (%(filename)s:%(lineno)d)"

        FORMATS = {
            logging.DEBUG: info_debug_color + format + reset,
            logging.INFO: info_debug_color + format + reset,
            logging.WARNING: yellow + format + reset,
            logging.ERROR: red + format + reset,
            logging.CRITICAL: bold_red + format + reset
        }

        def format(self, record):
            log_fmt = self.FORMATS.get(record.levelno)
            formatter = logging.Formatter(log_fmt)
            return formatter.format(record)

    ch = logging.StreamHandler()
    ch.setFormatter(CustomFormatter())

    levels = {
        "debug": logging.DEBUG,
        "info": logging.INFO,
        "warning": logging.WARNING,
        "error": logging.ERROR,
        "critical": logging.CRITICAL,
    }
    logger.setLevel(levels[level])
    ch.setLevel(levels[level])
    logger.addHandler(ch)

    return logger