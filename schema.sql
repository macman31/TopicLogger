CREATE TABLE `messages` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `type` enum('privmsg','notice','join','part','cmode','quit','kick','topic','subject','action','nick') NOT NULL,
  `timestamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `who` varchar(255) NOT NULL,
  `raw_nick` varchar(255) NOT NULL,
  `channel` varchar(255) NOT NULL,
  `body` varchar(512) NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB  DEFAULT CHARSET=utf8;