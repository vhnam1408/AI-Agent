
#pragma once

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Tên file là "priority.h" chữ thường; mọi include phải khớp chính xác.
// - Priority chỉ dùng để xếp hạng. Priority::High không được vượt hard constraints.
enum class Priority {
    Low,
    Mid,
    High
};

} // namespace edu_schedule
