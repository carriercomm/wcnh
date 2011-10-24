require 'pennmush-json'

module Ping
  PennJSON::register_object(self)

  REMOTE = PennJSON::Remote

  def self.pj_ping(arg)
    # Remote call example using context.
    result = REMOTE.pemit(REMOTE['enactor'], arg)

    # This ensures context was properly restored after remote call.
    return "#{result}->#{REMOTE['enactor']}"
  end

  def self.pj_div0
    # StandardError subclass example.
    return 1 / 0
  end

  def self.pj_exit
    # SystemExit example; not enabled by default, for obvious reasons.
    #exit
  end
end
