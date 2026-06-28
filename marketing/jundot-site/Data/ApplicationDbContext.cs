using Microsoft.EntityFrameworkCore;
using JundotSite.Models;

namespace JundotSite.Data;

public class ApplicationDbContext : DbContext
{
    public ApplicationDbContext(DbContextOptions<ApplicationDbContext> options)
        : base(options)
    {
    }

    public DbSet<SiteContent> SiteContents { get; set; }
    public DbSet<ReleaseVersion> ReleaseVersions { get; set; }
    public DbSet<ReleaseFeature> ReleaseFeatures { get; set; }
    public DbSet<ContactMessage> ContactMessages { get; set; }
    public DbSet<DownloadLog> DownloadLogs { get; set; }
    public DbSet<User> Users { get; set; }
    public DbSet<DocSnapshot> DocSnapshots { get; set; }
    public DbSet<EmailVerificationCode> EmailVerificationCodes { get; set; }

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<SiteContent>()
            .HasIndex(s => s.Key)
            .IsUnique();

        modelBuilder.Entity<ReleaseVersion>()
            .HasMany(r => r.Features)
            .WithOne(f => f.ReleaseVersion)
            .HasForeignKey(f => f.ReleaseVersionId)
            .OnDelete(DeleteBehavior.Cascade);

        modelBuilder.Entity<ReleaseVersion>()
            .HasIndex(r => r.VersionNumber)
            .IsUnique();

        modelBuilder.Entity<User>()
            .HasIndex(u => u.Email)
            .IsUnique();

        modelBuilder.Entity<User>()
            .HasIndex(u => u.Username)
            .IsUnique();

        modelBuilder.Entity<EmailVerificationCode>()
            .HasIndex(e => new { e.Email, e.Code, e.Purpose });

        modelBuilder.Entity<DocSnapshot>()
            .HasIndex(d => d.DocId);
    }
}
