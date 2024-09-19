# Manage Klipper extensions
#
# Copyright (C) 2024  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import os, subprocess, logging, uuid

EXT_STARTUP_WAIT = 5.

# Individual extension tracking
class ExtInstance:
    def __init__(self, printer, name):
        self.printer = printer
        self.name = name
        self.extdir = self.printer.get_start_args().get('extensions')
        self.proc = None
        self.uuid = str(uuid.uuid4())
        self.can_ack_config = False
        reactor = printer.get_reactor()
        self.ack_config_complete = reactor.completion()
    def get_uuid(self):
        return self.uuid
    def get_name(self):
        return self.name
    def handle_ack_config(self, webhooks):
        if not self.can_ack_config:
            raise web_request.error("Acknowledgment not available")
        error_msg = webhooks.get_str("error", None)
        settings = webhooks.get_dict("config", {})
        self.can_ack_config = False
        self.ack_config_complete.complete((error_msg, settings))
    def run_extension(self, config):
        config_error = self.printer.config_error
        # Run extension process
        self.can_ack_config = True
        prog = os.path.join(self.extdir, self.name, 'bin', 'python')
        apiaddr = self.printer.get_start_args().get('apiserver')
        params = [prog, '-m', self.name, apiaddr, self.uuid]
        logging.info("Invoking extension '%s'", self.name)
        try:
            self.proc = subprocess.Popen(params, stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, close_fds=True)
        except:
            logging.exception("Error on extension '%s' Popen call", self.name)
            raise config_error("Error running extension '%s'" % (self.name,))
        # Wait for extension to issue a "acknowledge_config" api call
        reactor = self.printer.get_reactor()
        did_exit = False
        starttime = systime = reactor.monotonic()
        while 1:
            res = self.ack_config_complete.wait(systime + 0.050)
            if res is not None:
                # Succesfully acked the config
                break
            exit_code = self.proc.poll()
            if exit_code is not None:
                if did_exit:
                    self.can_ack_config = False
                    raise config_error("Extension '%s' exited with code '%s'"
                                       % (self.name, exit_code))
                did_exit = True
            systime = reactor.monotonic()
            if systime - starttime > EXT_STARTUP_WAIT:
                self.can_ack_config = False
                raise config_error("Timeout waiting for extension '%s'"
                                   % (self.name,))
        # Note submitted config
        error_msg, settings = res
        if error_msg is not None:
            raise config_error("Extension '%s' error: %s"
                               % (self.name, error_msg))
        pconfig = self.printer.lookup_object("configfile")
        pconfig.claim_options(settings, self.name)
        logging.info("Successfully completed config for extension '%s'",
                     self.name)

# Global extension manager class
class ExtensionManager:
    def __init__(self, printer):
        self.printer = printer
        self.extensions = {}
        self.pending_uuids = {}
        self.extensions_by_client = {}
        start_args = printer.get_start_args()
        self.extdir = start_args.get('extensions')
        server_address = start_args.get('apiserver')
        is_fileinput = (start_args.get('debuginput') is not None)
        if not self.extdir or not server_address or is_fileinput:
            self.extdir = None
            return
        webhooks = printer.lookup_object('webhooks')
        webhooks.register_endpoint("extmgr/register_extension",
                                   self._handle_register)
        webhooks.register_endpoint("extmgr/acknowledge_config",
                                   self._handle_ack_config)
    def get_printer(self):
        return self.printer
    def load_object(self, config, section):
        if self.extdir is None:
            return
        module_name = section.split()[0]
        if module_name in self.extensions:
            return
        # Check if name looks like it could be an extension
        if not module_name.replace('_', '').isalnum():
            return
        # Check for existence of executable
        py_name = os.path.join(self.extdir, module_name, 'bin', 'python')
        if not os.path.exists(py_name):
            return
        # Create and run extension
        ext = ExtInstance(self.printer, module_name)
        self.extensions[module_name] = ext
        self.pending_uuids[ext.get_uuid()] = ext
        ext.run_extension(config)
    def validate_webhooks_ext(self, web_request):
        cconn = web_request.get_client_connection()
        ext = self.extensions_by_client.get(cconn)
        if ext is None:
            raise web_request.error("API call only available to extension")
        return ext
    def _handle_register(self, web_request):
        uuid = web_request.get_str("uuid")
        ext = self.pending_uuids.get(uuid)
        if ext is None:
            raise web_request.error("Extension not available for register")
        del self.pending_uuids[uuid]
        cconn = web_request.get_client_connection()
        self.extensions_by_client[cconn] = ext
        logging.info("Successfully registered extension '%s'", ext.get_name())
    def _handle_ack_config(self, web_request):
        ext = self.validate_webhooks_ext(web_request)
        ext.handle_ack_config(web_request)

def add_early_printer_objects(printer):
    printer.add_object('extmgr', ExtensionManager(printer))
