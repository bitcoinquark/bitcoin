#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Quark developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test cases for Bitcoin Quark fork """

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class BTQForkTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
        node = self.nodes[0]

        # Basic block generation test.
        # Block #1499.
        self.log.info("Generating 1499 blocks.")
        node.generate(1499)
        tmpl = node.getblocktemplate()
        assert_equal(tmpl['height'], 1500)

        # Block #3000, Equihash enabled.
        node.generate(1)
        tmpl = node.getblocktemplate()
        assert_equal(tmpl['height'], 1501)


if __name__ == '__main__':
    BTQForkTest().main()
