using JundotSite.Data;
using JundotSite.Models;
using Microsoft.EntityFrameworkCore;

namespace JundotSite.Services;

public class AuthService
{
    private readonly ApplicationDbContext _context;
    private readonly EmailService _emailService;
    private readonly ILogger<AuthService> _logger;
    private readonly Random _random = new();

    public AuthService(ApplicationDbContext context, EmailService emailService, ILogger<AuthService> logger)
    {
        _context = context;
        _emailService = emailService;
        _logger = logger;
    }

    public string GenerateVerificationCode()
    {
        return _random.Next(100000, 999999).ToString();
    }

    public async Task<(bool Success, string Message)> SendVerificationCodeAsync(string email, VerificationPurpose purpose)
    {
        var existingUser = await _context.Users.FirstOrDefaultAsync(u => u.Email == email);
        
        if (purpose == VerificationPurpose.Register && existingUser != null)
        {
            return (false, "该邮箱已被注册");
        }

        if (purpose == VerificationPurpose.Login && existingUser == null)
        {
            return (false, "该邮箱尚未注册");
        }

        // 作废旧验证码
        var oldCodes = await _context.EmailVerificationCodes
            .Where(c => c.Email == email && c.Purpose == purpose && !c.IsUsed)
            .ToListAsync();
        
        foreach (var code in oldCodes)
        {
            code.IsUsed = true;
        }

        var codeValue = GenerateVerificationCode();
        var verification = new EmailVerificationCode
        {
            Email = email,
            Code = codeValue,
            Purpose = purpose,
            CreatedAt = DateTime.Now,
            ExpiresAt = DateTime.Now.AddMinutes(10),
            IsUsed = false
        };

        _context.EmailVerificationCodes.Add(verification);
        await _context.SaveChangesAsync();

        var purposeText = purpose == VerificationPurpose.Register ? "注册" : "登录";
        var sent = await _emailService.SendVerificationCodeAsync(email, codeValue, purposeText);

        if (!sent)
        {
            return (false, "验证码发送失败，请检查邮箱地址或稍后重试");
        }

        return (true, "验证码已发送到您的邮箱");
    }

    public async Task<(bool Success, string Message, User? User)> VerifyCodeAsync(string email, string code, VerificationPurpose purpose)
    {
        var verification = await _context.EmailVerificationCodes
            .Where(c => c.Email == email && c.Code == code && c.Purpose == purpose && !c.IsUsed)
            .OrderByDescending(c => c.CreatedAt)
            .FirstOrDefaultAsync();

        if (verification == null)
        {
            return (false, "验证码错误", null);
        }

        if (verification.ExpiresAt < DateTime.Now)
        {
            return (false, "验证码已过期", null);
        }

        verification.IsUsed = true;
        await _context.SaveChangesAsync();

        if (purpose == VerificationPurpose.Register)
        {
            var user = new User
            {
                Username = $"User_{email.Split('@')[0]}",
                Email = email,
                PasswordHash = "",
                Role = UserRole.User,
                IsActive = true,
                CreatedAt = DateTime.Now
            };
            _context.Users.Add(user);
            await _context.SaveChangesAsync();
            _logger.LogInformation("新用户注册：{Email}", email);
            return (true, "注册成功", user);
        }

        var existingUser = await _context.Users.FirstOrDefaultAsync(u => u.Email == email);
        if (existingUser != null)
        {
            existingUser.LastLoginAt = DateTime.Now;
            await _context.SaveChangesAsync();
        }

        return (true, "验证成功", existingUser);
    }

    public async Task<(bool Success, string Message, User? User)> RegisterWithPasswordAsync(string username, string email, string password)
    {
        if (await _context.Users.AnyAsync(u => u.Email == email))
        {
            return (false, "该邮箱已被注册", null);
        }

        if (await _context.Users.AnyAsync(u => u.Username == username))
        {
            return (false, "该用户名已被使用", null);
        }

        var user = new User
        {
            Username = username.Trim(),
            Email = email.ToLower().Trim(),
            PasswordHash = BCrypt.Net.BCrypt.HashPassword(password),
            Role = UserRole.User,
            IsActive = true,
            CreatedAt = DateTime.Now
        };

        _context.Users.Add(user);
        await _context.SaveChangesAsync();

        _logger.LogInformation("新用户注册：{Username} ({Email})", username, email);
        return (true, "注册成功", user);
    }

    public async Task<(bool Success, string Message, User? User)> LoginWithPasswordAsync(string email, string password)
    {
        var user = await _context.Users.FirstOrDefaultAsync(u => u.Email == email.ToLower().Trim());

        if (user == null)
        {
            return (false, "用户不存在", null);
        }

        if (!user.IsActive)
        {
            return (false, "账号已被禁用", null);
        }

        if (string.IsNullOrEmpty(user.PasswordHash))
        {
            return (false, "请使用邮箱验证码登录", null);
        }

        if (!BCrypt.Net.BCrypt.Verify(password, user.PasswordHash))
        {
            return (false, "密码错误", null);
        }

        user.LastLoginAt = DateTime.Now;
        await _context.SaveChangesAsync();

        _logger.LogInformation("用户登录：{Username}", user.Username);
        return (true, "登录成功", user);
    }

    public async Task<User?> GetUserByIdAsync(int userId)
    {
        return await _context.Users.FindAsync(userId);
    }

    public async Task<User?> GetUserByEmailAsync(string email)
    {
        return await _context.Users.FirstOrDefaultAsync(u => u.Email == email.ToLower().Trim());
    }

    public async Task<List<User>> GetAllUsersAsync()
    {
        return await _context.Users.OrderByDescending(u => u.CreatedAt).ToListAsync();
    }

    public async Task<bool> SetUserPasswordAsync(int userId, string password)
    {
        var user = await _context.Users.FindAsync(userId);
        if (user == null) return false;

        user.PasswordHash = BCrypt.Net.BCrypt.HashPassword(password);
        await _context.SaveChangesAsync();
        return true;
    }

    public async Task<bool> ToggleUserActiveAsync(int userId)
    {
        var user = await _context.Users.FindAsync(userId);
        if (user == null) return false;

        user.IsActive = !user.IsActive;
        await _context.SaveChangesAsync();
        return true;
    }
}
