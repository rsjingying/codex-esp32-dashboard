from datetime import datetime


def clock_packet(moment=None):
    moment = moment or datetime.now()
    seconds = moment.hour*3600 + moment.minute*60 + moment.second
    return ('M:K:%s,%d' % (moment.strftime('%Y%m%d'), seconds)).encode('ascii')
