import base64
import ConfigParser
import io
import json
import os
import random
import sys
import time
import tornado.web
import traceback
import socket
import logging
from datetime import datetime
from datetime import timedelta

from token import TokenAuthenticator

sessions = {}
use_mock = True
config = None
global_session = None


class UserSession(object):
    clc = None
    def __init__(self, account, username, session_token, access_key, secret_key):
        self.obj_account = account
        self.obj_username = username
        self.obj_session_token = session_token
        self.obj_access_key = access_key
        self.obj_secret_key = secret_key
        self.obj_fullname = None
        self.session_start = time.time()
        self.session_last_used = time.time()
        self.session_lifetime_requests = 0

    @property
    def account(self):
        return self.obj_account

    @property
    def username(self):
        return self.obj_username

    @property
    def session_token(self):
        return self.obj_session_token

    @property
    def access_key(self):
        return self.obj_access_key

    @property
    def secret_key(self):
        return self.obj_secret_key

    @property
    def fullname(self):
        return self.obj_fullname

    @fullname.setter
    def fullname(self, name):
        self.obj_fullname = name

    # return only the info that's to be sent to browsers
    def get_session(self):
        return {'account':self.account, 'username': self.username, 'fullname': self.fullname}

class GlobalSession(object):
    def __init__(self):
        pass
    def get_value(self, scope, key):
        return config.get(scope, key)

    def list_items(self, scope):
        items = {}
        for k, v in config.items(scope):
            items[k] = v
        return items

    @property
    def language(self):
        return self.get_value('locale', 'language')

    @property
    def version(self):
        return '3.2.0'
    
    @property
    def admin_console_url(self):
        return 'https://' + self.get_value('server', 'clchost') + ':8443'

    @property
    def help_url(self):
        return self.get_value('locale', 'help.url')

    @property
    def admin_support_url(self):
        return self.get_value('locale', 'support.url')

    @property
    def instance_type(self):
        '''
        m1.small:1 128 2
        c1.medium: 2 128 5 74
        ... 
        '''
        m1_small = [self.get_value('instance_type','m1.small.cpu'),self.get_value('instance_type','m1.small.mem'),self.get_value('instance_type','m1.small.disk')]; 
        c1_medium = [self.get_value('instance_type','c1.medium.cpu'),self.get_value('instance_type','c1.medium.mem'),self.get_value('instance_type','c1.medium.disk')]; 
        m1_large = [self.get_value('instance_type','m1.large.cpu'),self.get_value('instance_type','m1.large.mem'),self.get_value('instance_type','m1.large.disk')];
        m1_xlarge = [self.get_value('instance_type','m1.xlarge.cpu'),self.get_value('instance_type','m1.xlarge.mem'),self.get_value('instance_type','m1.xlarge.disk')]; 
        c1_xlarge = [self.get_value('instance_type','c1.xlarge.cpu'),self.get_value('instance_type','c1.xlarge.mem'),self.get_value('instance_type','c1.xlarge.disk')]; 
        return {'m1.small':m1_small, 'c1.medium':c1_medium, 'm1.large':m1_large, 'm1.xlarge':m1_xlarge, 'c1.xlarge':c1_xlarge};

    # return the collection of global session info
    def get_session(self):
        return {
                'version': self.version,
                'language': self.language,
                'admin_console_url': self.admin_console_url,
                'help_url': self.help_url,
                'admin_support_url' : self.admin_support_url,
                'instance_type': self.instance_type,
               }

class EuiException(BaseException):
    def __init__(self, status_code, message):
        self.code = status_code
        self.msg = message

    @property
    def status_code(self):
        return self.code

    @status_code.setter
    def status_code(self, code):
        self.code = code

    @property
    def message(self):
        return self.msg

    @message.setter
    def message(self, msg):
        self.msg = msg

class CheckIpHandler(tornado.web.RequestHandler):
    def get(self):
        remote = self.request.remote_ip
        if remote == '::1':
            remote = '127.0.0.1'
        self.write(remote)

class BaseHandler(tornado.web.RequestHandler):
    user_session = None
    def should_use_mock(self):
        use_mock = config.getboolean('test', 'usemock')
        return use_mock

    def authorized(self):
        try:
            sid = self.get_cookie("session-id")
        except:
            return False

        if not sid or sid not in sessions:
            self.clear_cookie("session-id")
            self.clear_cookie("_xsrf")
            return False
        self.user_session = sessions[sid]
        return True

class RootHandler(BaseHandler):
    def get(self, path):
        try:
            path = os.path.join(config.get('paths', 'staticpath'), "index.html")
        except ConfigParser.Error:
            logging.info("Caught url path exception :"+path)
            path = '../static/index.html'
        self.render(path)

    def post(self, arg):
        action = self.get_argument("action")
        response = None
        try:
            if action == 'login':
                try:
                    response = LoginProcessor.post(self)
                except Exception, err:
                    traceback.print_exc(file=sys.stdout)
                    if isinstance(err, EuiException):
                        raise err
                    else:
                        raise EuiException(401, 'not authorized')
            elif action == 'init':
                try:
                    response = InitProcessor.post(self)
                except Exception, err:
                    traceback.print_exc(file=sys.stdout)
                    raise EuiException(401, 'not authorized')
            elif action == 'busy':
                if self.authorized():
                    self.user_session.session_last_used = time.time()
                    response = BusyResponse(self.user_session)
                else:
                    response = BusyResponse(None)
            else:
                if not self.authorized():
                    raise EuiException(401, 'not authorized')

                if action == 'session':
                    try:
                        response = SessionProcessor.post(self)
                    except Exception, err:
                        traceback.print_exc(file=sys.stdout)
                        raise EuiException(500, 'can\'t retrieve session info')
                elif action == 'logout':
                  try:
                      response = LogoutProcessor.post(self)
                  except Exception, err:
                      traceback.print_exc(file=sys.stdout)
                      raise EuiException(500, 'unknown error occured')
                else:
                    raise EuiException(500, 'unknown action')
        except EuiException, err:
            if err:
                raise tornado.web.HTTPError(err.status_code, err.message)
            else:
                raise tornado.web.HTTPError(500, 'unknown error occured')

        if not response:
            raise tornado.web.HTTPError(500, 'unknown error occured')

        self.write(response.get_response())

    def check_xsrf_cookie(self):
        action = self.get_argument("action")
        if action == 'login' or action == 'init':
            xsrf = self.xsrf_token
        else:
            super(RootHandler, self).check_xsrf_cookie()

class ProxyProcessor():
    @staticmethod
    def get(web_req):
        raise NotImplementedError("not supported")

    @staticmethod
    def post(web_req):
        raise NotImplementedError("not supported")

class LogoutProcessor(ProxyProcessor):
    @staticmethod
    def post(web_req):
        sid = web_req.get_cookie("session-id")
        if not sid or sid not in sessions:
            return LogoutResponse();
        terminateSession(sid)
        web_req.clear_cookie("session-id")
        web_req.clear_cookie("_xsrf")
        return LogoutResponse();

def terminateSession(id, expired=False):
    msg = 'logged out'
    if expired:
        msg = 'session timed out'
    logging.info("User %s after %d seconds" % (msg, (time.time() - sessions[id].session_start)));
    logging.info("--Proxy processed %d requests during this session", sessions[id].session_lifetime_requests)
    del sessions[id] # clean up session info

class LoginProcessor(ProxyProcessor):
    @staticmethod
    def post(web_req):
        auth_hdr = web_req.request.headers.get('Authorization')
        if not auth_hdr:
            raise NotImplementedError("auth header not found")
        if not auth_hdr.startswith('Basic '):
            raise NotImplementedError("auth header in wrong format")
        auth_decoded = base64.decodestring(auth_hdr[6:])
        account, user, passwd = auth_decoded.split(':', 3)
        remember = web_req.get_argument("remember")

        if config.getboolean('test', 'usemock') == False:
            auth = TokenAuthenticator(config.get('server', 'clchost'),
                            config.getint('server', 'session.abs.timeout')+60)
            creds = auth.authenticate(account, user, passwd)
            session_token = creds.session_token
            access_id = creds.access_key
            secret_key = creds.secret_key
        else:
            # assign bogus values so we never mistake them for the real thing (who knows?)
            session_token = "Larry"
            access_id = "Moe"
            secret_key = "Curly"

        # create session and store info there, set session id in cookie
        while True:
            sid = os.urandom(16).encode('hex')
            if sid in sessions:
                continue
            break
        web_req.set_cookie("session-id", sid)
        if remember == 'yes':
            expiration = datetime.now() + timedelta(days=180)
            web_req.set_cookie("account", account, expires=expiration)
            web_req.set_cookie("username", user, expires=expiration)
            web_req.set_cookie("remember", 'true' if remember else 'false', expires=expiration)
        else:
            web_req.clear_cookie("account")
            web_req.clear_cookie("username")
            web_req.clear_cookie("remember")
        sessions[sid] = UserSession(account, user, session_token, access_id, secret_key)

        return LoginResponse(sessions[sid])

class InitProcessor(ProxyProcessor):
    @staticmethod
    def post(web_req):
        language = config.get('locale','language')
        support_url = config.get('locale','support.url')
        if web_req.get_argument('host', False): 
          try:
            host = web_req.get_argument('host')
            ip_list = socket.getaddrinfo(host, 0, 0, 0, socket.SOL_TCP)
            for addr in ip_list:
              ip_str = (addr[4])[0]; 
              ip = ip_str.split('.');
              if (len(ip) == 4):
                return InitResponse(language, support_url, ip_str, host)
            raise Exception
          except:
            return InitResponse(language, support_url)
        else:
          return InitResponse(language, support_url)

class SessionProcessor(ProxyProcessor):
    @staticmethod
    def post(web_req):
        return LoginResponse(web_req.user_session)

class ProxyResponse(object):
    def __init__(self):
        pass

    def get_response(self):
        raise NotImplementedError( "Should have implemented this" )

class LogoutResponse(ProxyResponse):
    def __init__(self):
        pass
    def get_response(self):
        return {'result': 'success'}

class LoginResponse(ProxyResponse):
    def __init__(self, session):
        self.user_session = session

    def get_response(self):
        global global_session
        if not global_session:
            global_session = GlobalSession()

        return {'global_session': global_session.get_session(),
                'user_session': self.user_session.get_session()}

class BusyResponse(ProxyResponse):
    def __init__(self, session):
        self.user_session = session
    def get_response(self):
        if self.user_session:
            return {'result': 'true'}
        else:
            return {'result': 'false'}

class InitResponse(ProxyResponse):
    def __init__(self, lang, support_url, ip='', hostname=''):
        self.language = lang
        self.support_url = support_url
        self.ip = ip
        self.hostname = hostname

    def get_response(self):
        return {'language': self.language, 'support_url': self.support_url, 'ipaddr': self.ip, 'hostname': self.hostname}
