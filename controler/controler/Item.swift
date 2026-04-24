//
//  Item.swift
//  controler
//
//  Created by Gange on 2026/4/24.
//

import Foundation
import SwiftData

@Model
final class Item {
    var timestamp: Date
    
    init(timestamp: Date) {
        self.timestamp = timestamp
    }
}
